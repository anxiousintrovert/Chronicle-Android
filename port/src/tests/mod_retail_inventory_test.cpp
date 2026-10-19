#include <gtest/gtest.h>

#include <cstring>
#include <limits>

#include "mods/api.hpp"
#include "savedata.hpp"

namespace {
using Json = nlohmann::json;
using mods::GameCommand;

class ModRetailInventory : public testing::Test {
protected:
    std::unique_ptr<CSaveData> save;
    std::string                error;

    void SetUp() override { save = std::make_unique<CSaveData>(); }

    CUserStatus &Status() { return *save->GetDngStatus(); }

    bool Apply(std::vector<GameCommand> commands) { return mods::ApplySave(*save, commands, 0, error); }

    Json Read(const char *name, Json args) { return mods::ReadSave(*save, {1, 0, 0, true}, name, args); }
};

TEST_F(ModRetailInventory, BagGrantsUseNativeDefinitionsAndCountQuickSlots) {
    auto &s = Status();
    s.item_pack.num = 4;
    s.item_pack.quick_item_slot[0] = ITEM_BREAD;
    s.item_pack.quick_item_qty[0] = 2;
    ASSERT_TRUE(Apply({
        {"inventory.give", {{"item", ITEM_BREAD}, {"quantity", 2}, {"seed", 9}}}
    }))
        << error;
    EXPECT_EQ(Read("inventory.count", {
                                          {"item", ITEM_BREAD}
    })["quantity"],
              4);
    EXPECT_EQ(s.item_pack.item_count, 4);
    EXPECT_EQ(s.item_pack.item_vol[0], GetItemData(ITEM_BREAD)->vol);
    const auto before = std::make_unique<CSaveData>(*save);
    EXPECT_FALSE(Apply({
        {"inventory.give", {{"item", ITEM_BREAD}, {"quantity", 1}, {"seed", 9}}}
    }));
    EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    ASSERT_TRUE(Apply({
        {"inventory.remove", {{"item", ITEM_BREAD}, {"quantity", 3}}}
    }))
        << error;
    EXPECT_EQ(s.item_pack.item[0], -1);
    EXPECT_EQ(s.item_pack.quick_item_qty[0], 1);
    EXPECT_EQ(s.item_pack.item_count, 1);
}

TEST_F(ModRetailInventory, FailedGrantBatchRollsBackItemsMoneyAndGlobalRng) {
    const auto before = std::make_unique<CSaveData>(*save);
    srand(401);
    const int next = rand();
    srand(401);
    EXPECT_FALSE(Apply({
        {"inventory.give",   {{"item", ITEM_DRAN_S_FEATHER}, {"quantity", 1}, {"seed", 123}}},
        {"economy.add",      {{"amount", 50}}                                               },
        {"inventory.remove", {{"item", ITEM_BREAD}, {"quantity", 100}}                      }
    }));
    EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    EXPECT_EQ(rand(), next);
}

TEST_F(ModRetailInventory, ExplicitGrantSeedIsRepeatableAndNeverConsumesRetailRng) {
    auto                           copy = std::make_unique<CSaveData>(*save);
    const std::vector<GameCommand> commands = {
        {"inventory.give", {{"item", ITEM_DRAN_S_FEATHER}, {"quantity", 2}, {"seed", 4294967295ULL}}},
        {"weapons.give",   {{"item", ITEM_WEAPON_BONE_RAPIER}, {"seed", 81}}                        }
    };
    srand(11);
    const int next = rand();
    srand(11);
    ASSERT_TRUE(Apply(commands)) << error;
    EXPECT_EQ(rand(), next);
    ASSERT_TRUE(mods::ApplySave(*copy, commands, 0, error)) << error;
    EXPECT_EQ(std::memcmp(save.get(), copy.get(), sizeof(CSaveData)), 0);
    EXPECT_GE(Status().item_pack.item_vol[0], 180 * 60);
    EXPECT_LE(Status().item_pack.item_vol[0], 359 * 60);
    const auto *d = GetWeaponData(ITEM_WEAPON_BONE_RAPIER);
    EXPECT_EQ(Status().chara_weapons[d->owner][1].attack, d->attack);
}

TEST_F(ModRetailInventory, AbsThresholdAndSynthesisAreBoundedWithoutLevelUp) {
    const auto before = Read("weapons.abs_get", {
                                                    {"slot", 0}
    });
    ASSERT_TRUE(Apply({
        {"weapons.abs_add", {{"slot", 0}, {"amount", 32767}, {"synthesis", 7}}}
    }))
        << error;
    const auto after = Read("weapons.abs_get", {
                                                   {"slot", 0}
    });
    EXPECT_EQ(after["abs"], after["threshold"]);
    EXPECT_EQ(after["level"], before["level"]);
    EXPECT_EQ(after["synthesis"], 7);
    Status().chara_weapons[0][0].synthesis_count = std::numeric_limits<int32_t>::max();
    EXPECT_FALSE(Apply({
        {"weapons.abs_add", {{"slot", 0}, {"amount", 0}, {"synthesis", 1}}}
    }));
    EXPECT_EQ(Status().chara_weapons[0][0].synthesis_count, std::numeric_limits<int32_t>::max());
}

TEST_F(ModRetailInventory, WorldMapOwnershipUsesNativeFlagNotBagSpace) {
    ASSERT_TRUE(Apply({
        {"inventory.give", {{"item", ITEM_WORLDMAP}, {"quantity", 1}, {"seed", 0}}}
    }))
        << error;
    EXPECT_EQ(Read("inventory.count", {
                                          {"item", ITEM_WORLDMAP}
    })["quantity"],
              1);
    EXPECT_EQ(Status().item_pack.item_count, 0);
    ASSERT_TRUE(Apply({
        {"inventory.remove", {{"item", ITEM_WORLDMAP}, {"quantity", 1}}}
    }))
        << error;
    EXPECT_EQ(Status().special_flag_238, 0);
    EXPECT_FALSE(Apply({
        {"inventory.remove", {{"item", ITEM_WORLDMAP}, {"quantity", 1}}}
    }));
}

TEST_F(ModRetailInventory, WeaponCapacityAndMissingOwnersRejectWithoutOverflow) {
    auto &s = Status();
    for (auto &w : s.chara_weapons[0]) {
        w.item_no = ITEM_WEAPON_DAGGER;
    }
    const auto before = s;
    EXPECT_FALSE(Apply({
        {"weapons.give", {{"item", ITEM_WEAPON_BONE_RAPIER}, {"seed", 1}}}
    }));
    EXPECT_EQ(std::memcmp(&s, &before, sizeof(s)), 0);
    EXPECT_FALSE(Apply({
        {"weapons.give", {{"item", ITEM_WEAPON_BONE_SLINGSHOT}, {"seed", 1}}}
    }));
    EXPECT_EQ(std::memcmp(&s, &before, sizeof(s)), 0);
}

TEST(ModRetailValidation, InvalidRetailIdsAndAmountsNeverReachLookupTables) {
    for (const auto &c : std::vector<GameCommand>{
             {"inventory.give",          {{"item", 131}, {"quantity", 1}, {"seed", 0}}},
             {"inventory.give",          {{"item", 148}, {"quantity", 1}}             },
             {"inventory.remove",        {{"item", 148}, {"quantity", -1}}            },
             {"weapons.give",            {{"item", 377}, {"seed", 0}}                 },
             {"weapons.give",            {{"item", 258}, {"seed", 4294967296ULL}}     },
             {"weapons.abs_add",         {{"slot", 11}, {"amount", 1}}                },
             {"items.native_definition", {{"item", "author.custom-blade"}}            }
    }) {
        EXPECT_FALSE(mods::ValidateOperation(c.operation, c.arguments).empty());
    }
}
} // namespace
