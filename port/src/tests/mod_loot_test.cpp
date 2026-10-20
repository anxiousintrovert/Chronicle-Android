#include <gtest/gtest.h>

#include <array>
#include <cstring>

#include "itemdata.hpp"
#include "dungeonparts.hpp"
#include "chronicle/mod_chest_loot.hpp"
#include "mods/api.hpp"
#include "mods/entities.hpp"
#include "mods/loot.hpp"
#include "monstorunit.hpp"
#include "savedata.hpp"

namespace {
using Json = nlohmann::json;

class ModLoot : public testing::Test {
protected:
    std::array<MONSTOR_MODEL, 167> original;
    std::unique_ptr<CSaveData>     save;
    std::string                    error;

    void SetUp() override {
        mods::RetainOwnedLoot({});
        std::memcpy(original.data(), MonstorTable, sizeof(MonstorTable));
        save = std::make_unique<CSaveData>();
    }

    void TearDown() override {
        mods::RetainOwnedLoot({});
        std::memcpy(MonstorTable, original.data(), sizeof(MonstorTable));
    }

    bool Apply(std::vector<mods::GameCommand> commands, bool ready = true) {
        return mods::ApplyEntityAndSave(*save, nullptr, commands, 0, error, nullptr, nullptr, nullptr, ready);
    }

    mods::GameCommand Set(std::string owner = "test.a", std::string field = "rare_item", int item = ITEM_BREAD) {
        return {
            "dungeon.loot_set", {{"monster", 0}, {field, item}},
             owner
        };
    }
};

TEST_F(ModLoot, ReadEmptyAndAbandonedTransactionsPreserveTablesSaveAndRng) {
    const auto before = std::make_unique<CSaveData>(*save);
    srand(41);
    const int next = rand();
    srand(41);
    for (int i = 0; i < 167; ++i) {
        auto read = mods::ReadLoot({
            {"monster", i}
        });
        EXPECT_EQ(read["rare_item"], original[i].rare_item);
        EXPECT_EQ(read["steal_item"], original[i].steal_item);
        EXPECT_EQ(read["scope"], "future_model_loads");
    }
    {
        mods::LootTransaction tx;
        ASSERT_TRUE(tx.Stage({Set()}, true, error));
    }
    ASSERT_TRUE(Apply({}));
    EXPECT_FALSE(mods::HasOwnedLoot());
    EXPECT_EQ(std::memcmp(original.data(), MonstorTable, sizeof(MonstorTable)), 0);
    EXPECT_EQ(std::memcmp(before.get(), save.get(), sizeof(CSaveData)), 0);
    EXPECT_EQ(rand(), next);
}

TEST_F(ModLoot, DisjointOwnersConflictResetAndRetirement) {
    ASSERT_TRUE(Apply({Set(), Set("test.b", "steal_item", ITEM_REPAIR_POWDER)})) << error;
    auto expected = original;
    expected[0].rare_item = ITEM_BREAD;
    expected[0].steal_item = ITEM_REPAIR_POWDER;
    EXPECT_EQ(std::memcmp(expected.data(), MonstorTable, sizeof(MonstorTable)), 0);
    EXPECT_EQ(mods::ReadLoot({
                  {"monster", 0}
    })["owners"]["steal_item"],
              "test.b");
    EXPECT_FALSE(Apply({Set("test.b")}));
    EXPECT_NE(error.find("test.a"), std::string::npos);
    EXPECT_FALSE(Apply({
        {"dungeon.loot_reset", {{"monster", 0}, {"field", "rare_item"}}, "test.b"}
    }));
    ASSERT_TRUE(Apply({
        {"dungeon.loot_reset", {{"monster", 0}, {"field", "rare_item"}}, "test.a"}
    }));
    EXPECT_EQ(MonstorTable[0].rare_item, original[0].rare_item);
    EXPECT_EQ(MonstorTable[0].steal_item, ITEM_REPAIR_POWDER);
    mods::RetainOwnedLoot({});
    EXPECT_FALSE(mods::HasOwnedLoot());
    EXPECT_EQ(std::memcmp(original.data(), MonstorTable, sizeof(MonstorTable)), 0);
}

TEST_F(ModLoot, ExternalChangesSurviveReleaseAndReacquisition) {
    ASSERT_TRUE(Apply({Set()}));
    MonstorTable[0].rare_item = ITEM_REPAIR_POWDER;
    ASSERT_TRUE(Apply({Set()}));
    mods::RetainOwnedLoot({});
    EXPECT_EQ(MonstorTable[0].rare_item, ITEM_REPAIR_POWDER);
    ASSERT_TRUE(Apply({Set()}));
    MonstorTable[0].rare_item = -1;
    mods::RetainOwnedLoot({});
    EXPECT_EQ(MonstorTable[0].rare_item, -1);
}

TEST_F(ModLoot, OrderedReleaseAndTransferKeepOriginalBaseline) {
    ASSERT_TRUE(Apply({
        Set(), Set("test.a", "rare_item", ITEM_REPAIR_POWDER), {"dungeon.loot_reset", {{"monster", 0}, {"field", "rare_item"}}, "test.a"},
            Set("test.b"), Set("test.a", "steal_item")
    }))
        << error;
    const std::array<std::string, 1> retained{"test.b"};
    mods::RetainOwnedLoot(retained);
    EXPECT_EQ(MonstorTable[0].rare_item, ITEM_BREAD);
    EXPECT_EQ(MonstorTable[0].steal_item, original[0].steal_item);
    mods::RetainOwnedLoot({});
    EXPECT_EQ(std::memcmp(original.data(), MonstorTable, sizeof(MonstorTable)), 0);
}

TEST_F(ModLoot, MixedFailureDoesNotPublishSaveFieldsOrClaims) {
    auto before = std::make_unique<CSaveData>(*save);
    EXPECT_FALSE(Apply({
        Set(), {"not.an.operation", Json::object(), "test.a"}
    }));
    EXPECT_FALSE(mods::HasOwnedLoot());
    EXPECT_EQ(std::memcmp(original.data(), MonstorTable, sizeof(MonstorTable)), 0);
    EXPECT_EQ(std::memcmp(before.get(), save.get(), sizeof(CSaveData)), 0);
    EXPECT_FALSE(Apply({Set(), Set("test.b")}));
    EXPECT_FALSE(mods::HasOwnedLoot());
    EXPECT_FALSE(Apply({Set()}, false));
    EXPECT_FALSE(Apply({Set("")}));
    EXPECT_FALSE(Apply({
        Set(), {"combat.heal", {{"entity", "999"}, {"amount", 1}}, "test.a"}
    }));
    EXPECT_FALSE(Apply({
        Set(), {"economy.spend", {{"amount", 65535}}, "test.a"}
    }));
    EXPECT_FALSE(mods::HasOwnedLoot());
    EXPECT_EQ(std::memcmp(original.data(), MonstorTable, sizeof(MonstorTable)), 0);
}

TEST_F(ModLoot, StrictSchemaAndNativeDefinitionValidation) {
    EXPECT_TRUE(mods::ValidateOperation("dungeon.loot_get", {
                                                                {"monster", 166}
    })
                    .empty());
    for (auto bad : {Json(-1), Json(167), Json(0.5), Json("0"), Json(true)}) {
        EXPECT_FALSE(mods::ValidateLoot("dungeon.loot_get", {
                                                                {"monster", bad}
        })
                         .empty());
    }
    for (int item : {-2, 0, 80, static_cast<int>(ITEM_ATTACH_SYNTHESIS_SPHERE), 450}) {
        EXPECT_FALSE(mods::ValidateLoot("dungeon.loot_set", {
                                                                {"monster",   0   },
                                                                {"rare_item", item}
        })
                         .empty());
    }
    EXPECT_FALSE(mods::ValidateLoot("dungeon.loot_set", {
                                                            {"monster", 0}
    })
                     .empty());
    EXPECT_FALSE(mods::ValidateLoot("dungeon.loot_set", {
                                                            {"monster",     0   },
                                                            {"drops_items", true}
    })
                     .empty());
    EXPECT_FALSE(mods::ValidateLoot("dungeon.loot_reset", {
                                                              {"monster", 0    },
                                                              {"field",   "all"}
    })
                     .empty());
    ASSERT_TRUE(Apply({Set("test.a", "rare_item", -1)}));
    EXPECT_EQ(MonstorTable[0].rare_item, -1);
}

TEST_F(ModLoot, EmptyRuntimeRetiresClaimsBeforeGameplayEarlyReturn) {
    ASSERT_FALSE(mods::Scripts().HasGameplay());
    ASSERT_TRUE(Apply({Set()}));
    mods::TickGame();
    EXPECT_FALSE(mods::HasOwnedLoot());
    EXPECT_EQ(std::memcmp(original.data(), MonstorTable, sizeof(MonstorTable)), 0);
}

TEST_F(ModLoot, NativePickupCapacityBoundariesRemainVisible) {
    auto &status = *save->GetDngStatus();
    status.item_pack.num = 100;
    for (auto &item : status.item_pack.item) {
        item = ITEM_BREAD;
    }
    EXPECT_EQ(status.CheckItemGet(ITEM_BREAD), 1);
    for (auto &item : status.consumable_items) {
        item.id = ITEM_ATTACH_PERIDOT;
    }
    EXPECT_EQ(status.CheckItemGet(ITEM_ATTACH_PERIDOT), 2);
    for (auto &weapon : status.chara_weapons[0]) {
        weapon.item_no = ITEM_WEAPON_DAGGER;
    }
    // Native acceptance has no weapon-capacity test. A full eleven-slot array
    // must not be advertised as a successful grant merely because it accepts.
    EXPECT_EQ(status.CheckItemGet(ITEM_WEAPON_DAGGER), 0);
    const auto before = std::make_unique<CSaveData>(*save);
    EXPECT_EQ(status.GetItem(ITEM_WEAPON_DAGGER, 0), -1);
    EXPECT_EQ(std::memcmp(before.get(), save.get(), sizeof(CSaveData)), 0);
    EXPECT_TRUE(mods::ValidateLoot("dungeon.loot_set", {
                                                           {"monster",   0                 },
                                                           {"rare_item", ITEM_WEAPON_DAGGER}
    })
                    .empty());
}
mods::GameCommand Chest(std::string owner = "test.a", bool back = false, std::string kind = "small") {
    return {"dungeon.chest_loot_set", {{"dungeon", 0}, {"back", back}, {"kind", kind},
        {"entries", {{{"item", kind == "small" ? ITEM_BREAD : ITEM_WEAPON_DAGGER}, {"weight", 1}}}}}, owner};
}

TEST_F(ModLoot, ChestPoolsAreOwnedAtomicAndRetiredWithoutNativeTableWrites) {
    const Json selector = {{"dungeon", 0}, {"back", false}, {"kind", "small"}};
    int item = -100;
    srand(42); const auto next = rand(); srand(42);
    EXPECT_FALSE(mods::TryOwnedChestLoot(0, 0, 1, item));
    EXPECT_EQ(item, -100);
    EXPECT_EQ(rand(), next);
    { mods::LootTransaction tx; ASSERT_TRUE(tx.Stage({Chest()}, false, error)); }
    EXPECT_FALSE(mods::ReadChestLoot(selector)["enabled"]);
    ASSERT_TRUE(Apply({Chest()}, false)) << error;
    EXPECT_EQ(mods::ReadChestLoot(selector)["owner"], "test.a");
    // An unrelated save failure must not commit a second pool.
    EXPECT_FALSE(Apply({Chest("test.b", true), {"economy.spend", {{"amount", 65535}}, "test.b"}}, false));
    EXPECT_FALSE(mods::TryOwnedChestLoot(0, 1, 1, item));
    EXPECT_FALSE(Apply({Chest("test.b")}));
    EXPECT_NE(error.find("test.a"), std::string::npos);
    ASSERT_TRUE(Apply({Chest("test.b", true), Chest("test.c", false, "weapon")}));
    // Call the native function, not just the bridge: it must choose the owned pool.
    EXPECT_EQ(PresetSmallItemNo_Get(0, 0, 0, 1), ITEM_BREAD);
    EXPECT_EQ(PresetSmallItemNo_Get(0, 98, 1, 1), ITEM_BREAD);
    EXPECT_EQ(PresetSmallItemNo_Get(0, 0, 0, 0), ITEM_WEAPON_DAGGER);
    EXPECT_EQ(std::memcmp(original.data(), MonstorTable, sizeof(MonstorTable)), 0);
    EXPECT_FALSE(Apply({{"dungeon.chest_loot_reset", selector, "test.b"}}));
    ASSERT_TRUE(Apply({{"dungeon.chest_loot_reset", selector, "test.a"}}));
    EXPECT_FALSE(mods::TryOwnedChestLoot(0, 0, 1, item));
    mods::RetainOwnedLoot({});
    EXPECT_FALSE(mods::HasOwnedLoot());
    EXPECT_FALSE(mods::TryOwnedChestLoot(0, 1, 1, item));
}

TEST_F(ModLoot, ChestWeightsUseOneDeterministicRollAndValidateKinds) {
    auto command = Chest();
    command.arguments["entries"].push_back({{"item", ITEM_REPAIR_POWDER}, {"weight", 3}});
    ASSERT_TRUE(Apply({command}));
    srand(51);
    const auto roll = std::uint64_t(rand()) * 4 / 2147483648ULL;
    const int next = rand();
    srand(51);
    EXPECT_EQ(PresetSmallItemNo_Get(0, 0, 0, 1), roll < 1 ? ITEM_BREAD : ITEM_REPAIR_POWDER);
    EXPECT_EQ(rand(), next);
    for (const auto &entry : std::vector<Json>{
        {{"item", ITEM_BREAD}, {"weight", 0}}, {{"item", ITEM_BREAD}, {"weight", 10001}},
        {{"item", ITEM_WEAPON_DAGGER}, {"weight", 1}}, {{"item", ITEM_ATTACH_SYNTHESIS_SPHERE}, {"weight", 1}},
        {{"item", -1}, {"weight", 1}}, {{"item", ITEM_BREAD}, {"weight", 1.5}}}) {
        auto invalid = Chest(); invalid.arguments["entries"] = Json::array({entry});
        EXPECT_FALSE(mods::ValidateOperation(invalid.operation, invalid.arguments).empty());
    }
    auto invalid = Chest(); invalid.arguments["entries"].push_back(invalid.arguments["entries"][0]);
    EXPECT_FALSE(mods::ValidateOperation(invalid.operation, invalid.arguments).empty());
    invalid = Chest(); invalid.arguments["entries"] = Json::array();
    EXPECT_FALSE(mods::ValidateOperation(invalid.operation, invalid.arguments).empty());
    invalid = Chest(); invalid.arguments["dungeon"] = 7;
    EXPECT_FALSE(mods::ValidateOperation(invalid.operation, invalid.arguments).empty());
    invalid = Chest(); invalid.arguments["back"] = 1;
    EXPECT_FALSE(mods::ValidateOperation(invalid.operation, invalid.arguments).empty());
}
} // namespace
