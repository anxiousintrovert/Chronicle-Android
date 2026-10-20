#include <gtest/gtest.h>

#include <cstring>
#include <memory>

#include "battlemenu.hpp"
#include "main.hpp"
#include "menu_inventory.hpp"
#include "mods/api.hpp"
#include "mods/weapon_loadout.hpp"
#include "savedata.hpp"

extern s32 mode;

namespace {
class ModWeaponLoadout : public testing::Test {
protected:
    std::unique_ptr<CSaveData> save = std::make_unique<CSaveData>();
    std::string                error;

    CUserStatus &Status() { return *save->GetDngStatus(); }

    void SetUp() override {
        auto &s = Status();
        s.cur_floor = -1;
        s.cur_georama = 0;
        s.cur_chara = 0;
        s.party_size = 6;
        for (int character = 0; character < 6; ++character) {
            WepDataListToHaveCopy(GetDefaultWeaponNo(character) + 1, &s.chara_weapons[character][0]);
            s.chara_weapons[character][10] = s.chara_weapons[character][0];
            s.equipped_weapon_slot[character] = 0;
        }
    }

    mods::GameCommand Equip(int character = 0, int slot = 10) {
        return {
            "weapons.equip", {{"character", character}, {"slot", slot}},
             "test.loadout"
        };
    }

    bool Apply(std::vector<mods::GameCommand> commands, bool town = true, int money = 0) {
        return mods::ApplySave(*save, commands, money, error, nullptr, nullptr, nullptr, town);
    }
};

TEST_F(ModWeaponLoadout, ExactSchemaRequiresAnOwnedSlotAndOptionalCharacter) {
    EXPECT_TRUE(mods::ValidateOperation("weapons.equip", {
                                                             {"slot", 0}
    })
                    .empty());
    EXPECT_TRUE(mods::ValidateOperation("weapons.equip", {
                                                             {"slot",      10},
                                                             {"character", 5 }
    })
                    .empty());
    EXPECT_FALSE(mods::ValidateOperation("weapons.equip", nlohmann::json::object()).empty());
    for (nlohmann::json slot : {nlohmann::json(-1), nlohmann::json(11), nlohmann::json(1.0), nlohmann::json(true)}) {
        EXPECT_FALSE(mods::ValidateOperation("weapons.equip", {
                                                                  {"slot", slot}
        })
                         .empty());
    }
    EXPECT_FALSE(mods::ValidateOperation("weapons.equip", {
                                                              {"slot", 0  },
                                                              {"item", 258}
    })
                     .empty());
    EXPECT_FALSE(mods::ValidateOperation("weapons.equip_state", {
                                                                    {"character", 0}
    })
                     .empty());
}

TEST_F(ModWeaponLoadout, AllSixOwnersAndDuplicateCopiesSelectExactSlotWithoutTouchingWeaponsOrRng) {
    auto expected = std::make_unique<CSaveData>(*save);
    srand(623);
    const int next = rand();
    srand(623);
    for (int character = 0; character < 6; ++character) {
        ASSERT_TRUE(Apply({Equip(character)})) << error;
        expected->GetDngStatus()->equipped_weapon_slot[character] = 10;
    }
    EXPECT_EQ(std::memcmp(save.get(), expected.get(), sizeof(CSaveData)), 0);
    EXPECT_EQ(rand(), next);
    ASSERT_TRUE(Apply({Equip(0)}));
    EXPECT_EQ(std::memcmp(save.get(), expected.get(), sizeof(CSaveData)), 0);
    ASSERT_TRUE(Apply({
        {"weapons.equip", {{"slot", 0}}, "test.loadout"}
    }));
    EXPECT_EQ(Status().equipped_weapon_slot[0], 0);
    EXPECT_EQ(Status().equipped_weapon_slot[1], 10);
}

TEST_F(ModWeaponLoadout, RequiresExplicitTownContextAndRejectsLiveEquipmentContext) {
    const auto before = std::make_unique<CSaveData>(*save);
    EXPECT_FALSE(Apply({Equip()}, false));
    EXPECT_FALSE(mods::ApplyStatus(Status(), {Equip()}, 0, error));
    const mods::EquipmentContext equipment{0, Status().chara_weapons[0][0].item_no, 0};
    EXPECT_FALSE(mods::ApplySave(*save, {Equip()}, 0, error, nullptr, nullptr, &equipment, true));
    EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    // A previous dungeon or a new save can retain fields unrelated to the loaded town.
    Status().cur_floor = 5;
    Status().cur_georama = -1;
    ASSERT_TRUE(Apply({Equip()}));
    EXPECT_EQ(Status().cur_floor, 5);
    EXPECT_EQ(Status().cur_georama, -1);
}

TEST_F(ModWeaponLoadout, LiveReadinessRejectsDungeonDespiteTownLikeSaveFields) {
    const int previous_mode = mode;
    mode = GAME_MODE_DUNGEON;
    EXPECT_FALSE(mods::TownLoadoutReady());
    EXPECT_FALSE(mods::ReadWeaponEquipState()["ready"]);
    mode = previous_mode;
}

TEST_F(ModWeaponLoadout, RejectsEmptyIncompatibleUnjoinedAndInvalidNativeSlots) {
    Status().chara_weapons[0][10].item_no = -1;
    EXPECT_FALSE(Apply({Equip()}));
    Status().chara_weapons[0][10] = Status().chara_weapons[1][0];
    EXPECT_FALSE(Apply({Equip()}));
    Status().chara_weapons[0][10] = Status().chara_weapons[0][0];
    Status().equipped_weapon_slot[0] = 11;
    EXPECT_FALSE(Apply({Equip()}));
    Status().equipped_weapon_slot[0] = 0;
    Status().party_size = 1;
    EXPECT_FALSE(Apply({Equip(1)}));
    Status().party_size = 7;
    EXPECT_FALSE(Apply({Equip()}));
    EXPECT_EQ(Status().equipped_weapon_slot[0], 0);
}

TEST_F(ModWeaponLoadout, MixedFailureRollsBackSlotAndCurrencyAndEarlierWritesAreVisibleInBatch) {
    Status().money = 10;
    auto before = std::make_unique<CSaveData>(*save);
    EXPECT_FALSE(Apply({
                           Equip(), {"economy.spend", {{"amount", 1000}}, "test.loadout"}
    },
                       true, 5));
    EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    Status().chara_weapons[0][10].level = 4;
    ASSERT_TRUE(Apply({
        Equip(), {"weapons.abs_add", {{"character", 0}, {"slot", 10}, {"amount", 10}}, "test.loadout"}
    }))
        << error;
    EXPECT_EQ(Status().equipped_weapon_slot[0], 10);
    EXPECT_GE(Status().chara_weapons[0][10].experience, 10);
    Status().chara_weapons[0][1].item_no = -1;
    Status().chara_weapons[0][10].item_no = -1;
    Status().equipped_weapon_slot[0] = 0;
    ASSERT_TRUE(Apply({
        {"weapons.give", {{"item", ITEM_WEAPON_BONE_RAPIER}, {"seed", 17}}, "test.loadout"},
        Equip(0, 1)
    }))
        << error;
    EXPECT_EQ(Status().equipped_weapon_slot[0], 1);
    EXPECT_EQ(Status().chara_weapons[0][1].item_no, ITEM_WEAPON_BONE_RAPIER);
}
} // namespace
