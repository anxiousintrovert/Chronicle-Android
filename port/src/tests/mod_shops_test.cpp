#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>

#include "editmapscript.hpp"
#include "gamemode.hpp"
#include "main.hpp"
extern s32 mode;

#include "itemdata.hpp"
#include "mods/api.hpp"
#include "mods/entities.hpp"
#include "mods/runtime.hpp"
#include "mods/shops.hpp"
#include "savedata.hpp"
#include "shop.hpp"

namespace {
using Json = nlohmann::json;

class ModShops : public testing::Test {
protected:
    std::unique_ptr<CSaveData> save;
    std::string                error;

    void SetUp() override {
        mods::RetainOwnedShops({});
        save = std::make_unique<CSaveData>();
    }

    void TearDown() override { mods::RetainOwnedShops({}); }

    Json All() { return mods::ReadShop("shops.list", Json::object()); }

    Json Get(int shop = 0) { return mods::ReadShop("shops.get", {
                                                                    {"shop", shop}
 }); }

    bool Apply(std::vector<mods::GameCommand> commands, bool writable = true) {
        return mods::ApplyEntityAndSave(*save, nullptr, commands, 0, error, nullptr, nullptr, nullptr, writable);
    }

    mods::GameCommand Stock(Json items, std::string owner = "test.a", int shop = 0) {
        return {
            "shops.set_items", {{"shop", shop}, {"items", items}},
             owner
        };
    }

    mods::GameCommand Price(std::string owner = "test.a") {
        return {
            "shops.set_price", {{"item", ITEM_BREAD}, {"buy", 77}, {"sell", 33}},
             owner
        };
    }
};

TEST_F(ModShops, ReadAndAbandonedStageLeaveNativeTablesSaveAndRngUntouched) {
    const auto before = All();
    ASSERT_EQ(before["shops"].size(), 18u);
    EXPECT_EQ(before["shops"][0]["items"].size(), 20u);
    const auto saved = std::make_unique<CSaveData>(*save);
    srand(19);
    const int next = rand();
    srand(19);
    {
        mods::ShopTransaction tx;
        ASSERT_TRUE(tx.Stage({Stock({ITEM_BREAD}), Price()}, true, error)) << error;
    }
    EXPECT_EQ(All(), before);
    EXPECT_FALSE(mods::HasOwnedShops());
    EXPECT_EQ(std::memcmp(save.get(), saved.get(), sizeof(CSaveData)), 0);
    EXPECT_EQ(rand(), next);
}

TEST_F(ModShops, OwnerChangesRestoreExactStockAndSharedPricesAndReportConflicts) {
    const auto before = All();
    ASSERT_TRUE(Apply({Stock({ITEM_BREAD, ITEM_BREAD, ITEM_REPAIR_POWDER}), Price()})) << error;
    auto changed = All();
    EXPECT_EQ(Get()["owner"], "test.a");
    EXPECT_EQ(Get()["items"].size(), 3u);
    for (const auto &shop : changed["shops"]) {
        for (const auto &item : shop["items"]) {
            if (item["item"] == ITEM_BREAD) {
                EXPECT_EQ(item["buy"], 77);
                EXPECT_EQ(item["sell"], 33);
                EXPECT_EQ(item["price_owner"], "test.a");
            }
        }
    }
    EXPECT_FALSE(Apply({Stock({ITEM_BREAD}, "test.b")}));
    EXPECT_NE(error.find("test.a"), std::string::npos);
    EXPECT_FALSE(Apply({Price("test.b")}));
    EXPECT_NE(error.find("test.a"), std::string::npos);
    EXPECT_FALSE(Apply({
        {"shops.reset_price", {{"item", ITEM_BREAD}}, "test.b"}
    }));
    EXPECT_EQ(All(), changed);
    ASSERT_TRUE(Apply({Stock({ITEM_BREAD})})) << error;
    ASSERT_TRUE(Apply({
        {"shops.reset_items", {{"shop", 0}},          "test.a"},
        {"shops.reset_price", {{"item", ITEM_BREAD}}, "test.a"}
    }))
        << error;
    EXPECT_EQ(All(), before);
    EXPECT_FALSE(mods::HasOwnedShops());
}

TEST_F(ModShops, ShopSaveAndEntityFailuresPublishNoChangesOrOwnership) {
    const auto before = All();
    const auto saved = std::make_unique<CSaveData>(*save);
    EXPECT_FALSE(Apply({
        Stock({ITEM_BREAD}
        ), Price(), {"economy.spend", {{"amount", 65535}}}
    }));
    EXPECT_EQ(All(), before);
    EXPECT_EQ(std::memcmp(save.get(), saved.get(), sizeof(CSaveData)), 0);
    EXPECT_FALSE(mods::HasOwnedShops());
    EXPECT_FALSE(Apply({
        Stock({ITEM_BREAD}
        ), {"combat.heal", {{"entity", "999"}, {"amount", 1}}, "test.a"}
    }));
    EXPECT_EQ(All(), before);
    EXPECT_FALSE(mods::HasOwnedShops());
    EXPECT_FALSE(Apply({Stock({ITEM_BREAD}), Price()}, false));
    EXPECT_EQ(All(), before);
    EXPECT_FALSE(Apply({Price("")}));
    EXPECT_EQ(All(), before);
}

TEST_F(ModShops, AppendRemovalAndCapacityUseEarlierStagedListPreservingDuplicates) {
    ASSERT_TRUE(Apply({
        Stock({ITEM_BREAD, ITEM_BREAD}
        ),
        {"shops.register_item", {{"shop", 0}, {"item", ITEM_REPAIR_POWDER}}, "test.a"},
        {"shops.remove_item", {{"shop", 0}, {"item", ITEM_BREAD}}, "test.a"}
    }))
        << error;
    EXPECT_EQ(Get()["items"].size(), 2u);
    EXPECT_EQ(Get()["items"][0]["item"], ITEM_BREAD);
    EXPECT_EQ(Get()["items"][1]["item"], ITEM_REPAIR_POWDER);
    const auto before = All();
    EXPECT_FALSE(Apply({
        {"shops.remove_item", {{"shop", 0}, {"item", ITEM_CHEESE}}, "test.a"}
    }));
    EXPECT_EQ(All(), before);
    Json full = Json::array();
    for (int i = 0; i < 20; ++i) {
        full.push_back(ITEM_BREAD);
    }
    EXPECT_FALSE(Apply({
        Stock(full), {"shops.register_item", {{"shop", 0}, {"item", ITEM_CHEESE}}, "test.a"}
    }));
    EXPECT_EQ(All(), before);
    ASSERT_TRUE(Apply({Stock(Json::array())})) << error;
    EXPECT_TRUE(Get()["items"].empty());
}

TEST_F(ModShops, CleanupRetainsOtherOwnersAndPreservesExternalNativeChanges) {
    std::array<s16, 20> original;
    std::copy_n(GetItemShopList(0), 20, original.begin());
    ASSERT_TRUE(Apply({Stock({ITEM_CHEESE}), Stock({ITEM_BREAD}, "test.b", 1), Price("test.b")})) << error;
    GetItemShopList(0)[0] = ITEM_REPAIR_POWDER;
    const std::vector<std::string> active{"test.b"};
    mods::RetainOwnedShops(active);
    EXPECT_EQ(GetItemShopList(0)[0], ITEM_REPAIR_POWDER);
    EXPECT_EQ(Get()["owner"], "");
    EXPECT_EQ(Get(1)["owner"], "test.b");
    EXPECT_TRUE(mods::HasOwnedShops());
    // Restore our external test edit explicitly; the ownership cleanup correctly left it alone.
    std::copy(original.begin(), original.end(), GetItemShopList(0));
    mods::RetainOwnedShops({});
    EXPECT_FALSE(mods::HasOwnedShops());
}

TEST_F(ModShops, EmptyRuntimeRestoresOwnedNativeTables) {
    const auto before = All();
    ASSERT_FALSE(mods::Scripts().HasGameplay());
    ASSERT_TRUE(Apply({Stock({ITEM_BREAD}), Price()})) << error;
    mods::TickGame();
    EXPECT_EQ(All(), before);
    EXPECT_FALSE(mods::HasOwnedShops());
}

TEST_F(ModShops, CleanupDefersWhileTownMenuIsOpenAndRestoresOnReturnToWalking) {
    const auto before = All();
    ASSERT_TRUE(Apply({Stock({ITEM_BREAD}), Price()})) << error;
    const auto changed = All();
    const auto old_mode = mode;
    const auto old_town = GameMode;
    mode = GAME_MODE_EDIT;
    GameMode = ED_MODE_MENU;
    mods::TickGame();
    EXPECT_EQ(All(), changed);
    EXPECT_TRUE(mods::HasOwnedShops());
    GameMode = ED_MODE_WALK;
    mods::TickGame();
    EXPECT_EQ(All(), before);
    EXPECT_FALSE(mods::HasOwnedShops());
    mode = old_mode;
    GameMode = old_town;
}

TEST(ModShopValidation, ExactSchemasRejectInvalidContentOverflowAndPrices) {
    EXPECT_TRUE(mods::ValidateOperation("shops.list", Json::object()).empty());
    EXPECT_TRUE(mods::ValidateOperation("shops.set_items", {
                                                               {"shop",  17           },
                                                               {"items", Json::array()}
    })
                    .empty());
    for (const auto &a : std::vector<Json>{
             {{"shop", 18}, {"items", {148}}},
             {{"shop", 0}, {"items", {131}}},
             {{"shop", 0}, {"items", {ITEM_ATTACH_SYNTHESIS_SPHERE}}},
             {{"shop", 0}, {"items", {148.5}}},
             {{"shop", 0}},
             {{"shop", 0}, {"items", {148}}, {"force", true}}
    }) {
        EXPECT_FALSE(mods::ValidateOperation("shops.set_items", a).empty());
    }
    EXPECT_FALSE(mods::ValidateOperation("shops.set_price", {
                                                                {"item", 148  },
                                                                {"buy",  32768},
                                                                {"sell", 1    }
    })
                     .empty());
    EXPECT_FALSE(mods::ValidateOperation("shops.set_price", {
                                                                {"item", 148},
                                                                {"buy",  0  },
                                                                {"sell", -1 }
    })
                     .empty());
    EXPECT_TRUE(mods::ValidateOperation("shops.set_price", {
                                                               {"item", 148  },
                                                               {"buy",  0    },
                                                               {"sell", 32767}
    })
                    .empty());
}
} // namespace
