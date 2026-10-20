#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <memory>

#include "battlemenu.hpp"
#include "menu_inventory.hpp"
#include "menu_misc.hpp"
#include "mods/api.hpp"
#include "savedata.hpp"

namespace {
class ModItemUse : public testing::Test {
protected:
    std::unique_ptr<CSaveData> save = std::make_unique<CSaveData>();
    std::string                error;

    CUserStatus &Status() { return *save->GetDngStatus(); }

    void SetUp() override {
        auto &s = Status();
        s.party_size = 6;
        s.cur_chara = 0;
        s.item_pack.num = 10;
        for (auto &item : s.item_pack.item) {
            item = -1;
        }
        for (auto &item : s.item_pack.quick_item_slot) {
            item = -1;
        }
        for (int i = 0; i < 6; ++i) {
            s.hp[i] = 10;
            s.max_hp[i] = 100;
            s.next_hp[i] = 25;
            s.life_step[i] = 1;
            s.water_now[i] = 12.75f;
            s.water_max[i] = 40;
            s.ailments[i] = AILMENT_FREEZE | AILMENT_POISON | AILMENT_CURSE | AILMENT_GOO | AILMENT_STAMINA;
            s.ailment_frames[i] = 123;
        }
    }

    mods::GameCommand Use(int item, int character = 0) {
        return {
            "items.use", {{"item", item}, {"character", character}},
             "test.items"
        };
    }

    bool Apply(std::vector<mods::GameCommand> commands) { return mods::ApplySave(*save, commands, 0, error); }
};

TEST_F(ModItemUse, MatchesNativeEffectsAndConsumptionForAllSupportedDefinitionsAndCharacters) {
    int       definitions = 0;
    const int old_volume = MenuItemUseVolume;
    for (int item = 132; item <= 256; ++item) {
        const auto *data = GetItemData(item);
        if (!data) {
            continue;
        }
        const bool food = data->kind_flags == ITEMKINDF_CONSUMABLE && (data->use_flags == ITEMUSE_HEAL_HP || data->use_flags == ITEMUSE_DRINK);
        const bool cure = data->kind_flags == 1 && (data->use_flags == 0x4000 || data->use_flags == 0x8000 || data->use_flags == 0x10000 || data->use_flags == 0x20000 || item == ITEM_MIGHTY_HEALING);
        if (!food && !cure) {
            continue;
        }
        ++definitions;
        for (int character = 0; character < 6; ++character) {
            for (bool quick : {false, true}) {
                SetUp();
                Status().item_pack.item[0] = quick ? -1 : item;
                Status().item_pack.item_vol[0] = 57;
                Status().item_pack.quick_item_slot[0] = item;
                Status().item_pack.quick_item_qty[0] = 1;
                auto expected = std::make_unique<CSaveData>(*save);
                ASSERT_EQ(ItemUseFunc(expected->GetDngStatus(), item, character, ITEM_USE_ON_CHARA, nullptr), 1) << item;
                DeleteItemAfterUseItem(item, &expected->GetDngStatus()->item_pack);
                MenuItemUseVolume = 4321;
                srand(917);
                const int next = rand();
                srand(917);
                ASSERT_TRUE(Apply({Use(item, character)})) << item << ": " << error;
                EXPECT_EQ(std::memcmp(save.get(), expected.get(), sizeof(CSaveData)), 0) << item << ": " << character;
                EXPECT_EQ(MenuItemUseVolume, 4321);
                EXPECT_EQ(rand(), next);
            }
        }
    }
    MenuItemUseVolume = old_volume;
    EXPECT_GE(definitions, 9);
}

TEST_F(ModItemUse, FailureAndLaterCommandRollbackPreserveWholeSave) {
    Status().item_pack.item[0] = ITEM_BREAD;
    auto before = std::make_unique<CSaveData>(*save);
    EXPECT_FALSE(Apply({Use(ITEM_BREAD), Use(ITEM_BREAD)}));
    EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    EXPECT_FALSE(Apply({
        Use(ITEM_BREAD), {"inventory.remove", {{"item", ITEM_BREAD}, {"quantity", 1}}}
    }));
    EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    EXPECT_FALSE(Apply({Use(ITEM_MELLOW_BANANA)}));
    EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
}

TEST_F(ModItemUse, RejectsIneffectiveInvalidAndOverflowOnlyItems) {
    auto reject = [&](int item) {
        auto before = std::make_unique<CSaveData>(*save);
        EXPECT_FALSE(Apply({Use(item)})) << item;
        EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    };
    Status().item_pack.item[10] = ITEM_BREAD;
    reject(ITEM_BREAD);
    Status().item_pack.item[0] = ITEM_BREAD;
    Status().hp[0] = Status().max_hp[0];
    reject(ITEM_BREAD);
    Status().hp[0] = 0;
    reject(ITEM_BREAD);
    Status().item_pack.item[0] = ITEM_REGULAR_WATER;
    Status().water_now[0] = 38.5f;
    reject(ITEM_REGULAR_WATER);
    Status().water_now[0] = std::numeric_limits<float>::quiet_NaN();
    reject(ITEM_REGULAR_WATER);
    Status().item_pack.item[0] = ITEM_MIGHTY_HEALING;
    Status().hp[0] = 10;
    Status().ailments[0] = AILMENT_KNOCKED_OUT | AILMENT_POISON;
    reject(ITEM_MIGHTY_HEALING);
    Status().ailments[0] = AILMENT_STAMINA;
    reject(ITEM_MIGHTY_HEALING);
    Status().item_pack.num = 101;
    reject(ITEM_MIGHTY_HEALING);
}

TEST_F(ModItemUse, SchemaAndDefaultCharacterUseTheSharedHostBoundary) {
    EXPECT_TRUE(mods::ValidateOperation("items.use", {
                                                         {"item", ITEM_BREAD}
    })
                    .empty());
    for (const auto &args : {
             nlohmann::json::object(), nlohmann::json{{"item", 148.0}},
              nlohmann::json{{"item", ITEM_BREAD}, {"quantity", 1}},
              nlohmann::json{{"item", ITEM_BREAD}, {"character", 6}}
    }) {
        EXPECT_FALSE(mods::ValidateOperation("items.use", args).empty());
    }
    Status().cur_chara = 5;
    ASSERT_TRUE(Apply({
        {"inventory.give", {{"item", ITEM_BREAD}, {"quantity", 1}, {"seed", 0}}},
        {"items.use",      {{"item", ITEM_BREAD}}                              }
    }))
        << error;
    EXPECT_EQ(Status().next_hp[5], 60);
    EXPECT_EQ(Status().next_hp[0], 25);
}
} // namespace
