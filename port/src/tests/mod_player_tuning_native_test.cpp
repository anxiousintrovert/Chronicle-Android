#include <gtest/gtest.h>

#include <bit>
#include <memory>

#include "dun/gameloop.hpp"
#include "hitvalue.hpp"
#include "main.hpp"
#include "mods/entities.hpp"
#include "mods/player_tuning_native.hpp"
#include "savedata.hpp"
#include "userstatus.hpp"

extern s32 mode;
int        BattleSubWeaponDmg(float amount, int kind);

namespace {
using Tuning = mods::PlayerTuning;

class ModPlayerTuningNative : public testing::Test {
protected:
    int                          old_mode = mode;
    CUserStatus                 *old_user = UserStatus;
    WEAPON_HAVE                 *old_weapon = NowWeaponHave;
    std::unique_ptr<CUserStatus> user = std::make_unique<CUserStatus>();
    WEAPON_HAVE                  weapon{};
    Tuning                       previous;

    void SetUp() override {
        previous.Swap(mods::OwnedPlayerTuning());
        mode = GAME_MODE_DUNGEON;
        UserStatus = user.get();
        NowWeaponHave = &weapon;
        user->cur_chara = 0;
        user->cur_georama = 0;
        user->water_now[0] = 10;
        user->res_limit_zone_current = RES_LIMIT_ZONE_NONE;
    }

    void TearDown() override {
        previous.Swap(mods::OwnedPlayerTuning());
        mode = old_mode;
        UserStatus = old_user;
        NowWeaponHave = old_weapon;
    }

    void Set(nlohmann::json args) {
        std::string                            error;
        const std::array<mods::GameCommand, 1> commands{{{"player.tuning_set", std::move(args), "test.tuning"}}};
        ASSERT_TRUE(mods::OwnedPlayerTuning().Stage(commands, 1, error)) << error;
    }
};

TEST_F(ModPlayerTuningNative, DormantAndNeutralPreserveFloatBitsAndNativeFlags) {
    for (float original : {0.0f, -0.0f, 1.45f, -23.25f}) {
        float value = original;
        mods::ScalePlayerValue(Tuning::RunSpeed, value);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(value), std::bit_cast<std::uint32_t>(original));
    }
    Set({
        {"run_speed",    1    },
        {"recoil_scale", 1    },
        {"map_reveal",   false}
    });
    float value = -0.0f;
    mods::ScalePlayerValue(Tuning::RunSpeed, value);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(value), std::bit_cast<std::uint32_t>(-0.0f));
    int recoil = 8, invincible = 160, map = 2, monsters = 0;
    mods::TunePlayerHitFrames(recoil, invincible);
    mods::ApplyPlayerMapReveal(map, monsters);
    EXPECT_EQ(recoil, 8);
    EXPECT_EQ(invincible, 160);
    EXPECT_EQ(map, 2);
    EXPECT_EQ(monsters, 0);
}

TEST_F(ModPlayerTuningNative, HitAndRevealOverridesApplyOnlyInDungeonAndClearCleanly) {
    Set({
        {"run_speed",               2   },
        {"recoil_scale",            0.25},
        {"invincible_bonus_frames", 120 },
        {"map_reveal",              true},
        {"monster_reveal",          true}
    });
    int recoil = 80, invincible = 0, map = 0, monsters = 0;
    mods::TunePlayerHitFrames(recoil, invincible);
    mods::ApplyPlayerMapReveal(map, monsters);
    EXPECT_EQ(recoil, 20);
    EXPECT_EQ(invincible, 140);
    EXPECT_EQ(map, 1);
    EXPECT_EQ(monsters, 1);
    invincible = 600;
    mods::TunePlayerHitFrames(recoil, invincible);
    EXPECT_EQ(invincible, 600);
    mode = GAME_MODE_EDIT;
    float speed = 1.45f;
    mods::ScalePlayerValue(Tuning::RunSpeed, speed);
    EXPECT_EQ(speed, 1.45f);
    mode = GAME_MODE_DUNGEON;
    mods::ScalePlayerValue(Tuning::RunSpeed, speed);
    EXPECT_EQ(speed, 2.9f);
    mods::OwnedPlayerTuning().Retain({}, 1);
    speed = 1.45f;
    mods::ScalePlayerValue(Tuning::RunSpeed, speed);
    EXPECT_EQ(speed, 1.45f);
}

TEST_F(ModPlayerTuningNative, NativeThirstRespectsRestrictionsFlagsAndDisabledDrain) {
    for (int dungeon : {0, 5}) {
        for (int restriction : {int(RES_LIMIT_ZONE_NONE), int(RES_LIMIT_ZONE_THIRST)}) {
            for (int flags : {0, 8, 16, 24}) {
                for (float scale : {0.25f, 1.0f, 4.0f}) {
                    Set({
                        {"thirst_drain", scale}
                    });
                    user->cur_georama = dungeon;
                    user->res_limit_zone_current = restriction;
                    weapon.flags = flags;
                    user->water_now[0] = 10;
                    float drain = 0.003f * (1.0f + 0.2f * dungeon);
                    if (restriction == RES_LIMIT_ZONE_THIRST) {
                        drain *= 5;
                    }
                    if (flags & 8) {
                        drain *= 0.8f;
                    }
                    if (flags & 16) {
                        drain *= 2;
                    }
                    if (scale != 1) {
                        drain *= scale;
                    }
                    user->Step(0);
                    EXPECT_FLOAT_EQ(user->water_now[0], 10 - drain);
                }
            }
        }
    }
    user->water_now[0] = 10;
    user->water_drain_disable = 1;
    user->Step(0);
    EXPECT_EQ(user->water_now[0], 10);
    user->water_drain_disable = 0;
    user->Step(1);
    EXPECT_EQ(user->water_now[0], 10);
}

TEST_F(ModPlayerTuningNative, NativeWearScalesBaseWearAndPreservesKindSurcharge) {
    auto &equipped = user->chara_weapons[0][0];
    user->equipped_weapon_slot[0] = 0;
    equipped.item_no = ITEM_WEAPON_DAGGER;
    equipped.durability = 100;
    for (int endurance : {0, 50, 99}) {
        for (int kind : {0, 1, 3}) {
            for (float scale : {0.25f, 1.0f, 4.0f}) {
                Set({
                    {"durability_wear", scale}
                });
                equipped.durability_f = 100;
                weapon.endurance = endurance;
                float wear = (1.5f - 0.01f * endurance) * 2;
                if (scale != 1) {
                    wear *= scale;
                }
                wear += 0.1f * kind;
                EXPECT_EQ(BattleSubWeaponDmg(2, kind), 0);
                EXPECT_FLOAT_EQ(equipped.durability_f, 100 - wear);
            }
        }
    }
    mods::OwnedPlayerTuning().Retain({}, 1);
    equipped.durability_f = 100;
    weapon.endurance = 0;
    EXPECT_EQ(BattleSubWeaponDmg(2, 0), 0);
    EXPECT_FLOAT_EQ(equipped.durability_f, 97);
}

TEST_F(ModPlayerTuningNative, SaveTransactionFailureCannotPublishTuningOrClearExistingClaims) {
    auto  save = std::make_unique<CSaveData>();
    auto &status = *save->GetDngStatus();
    status.money = 10;
    std::string error;
    const auto  before = mods::OwnedPlayerTuning().State();
    EXPECT_FALSE(mods::ApplyEntityAndSave(*save, nullptr,
                                          {
                                              {"player.tuning_set", {{"run_speed", 2}}, "one"},
                                              {"unsupported.write", {},                 "one"}
    },
                                          0, error, nullptr, nullptr));
    EXPECT_EQ(mods::OwnedPlayerTuning().State(), before);
    ASSERT_TRUE(mods::ApplyEntityAndSave(*save, nullptr,
                                         {
                                             {"player.tuning_set", {{"run_speed", 2}}, "one"}
    },
                                         5, error, nullptr, nullptr))
        << error;
    EXPECT_EQ(status.money, 15);
    EXPECT_EQ(mods::OwnedPlayerTuning().Value(Tuning::RunSpeed), 2);
    EXPECT_FALSE(mods::ApplyEntityAndSave(*save, nullptr,
                                          {
                                              {"player.tuning_clear", nlohmann::json::object(), "one"},
                                              {"unsupported.write",   {},                       "one"}
    },
                                          5, error, nullptr, nullptr));
    EXPECT_EQ(status.money, 15);
    EXPECT_EQ(mods::OwnedPlayerTuning().Value(Tuning::RunSpeed), 2);
    ASSERT_FALSE(mods::Scripts().HasGameplay());
    mods::RetainPlayerTuningOwners();
    EXPECT_FALSE(mods::OwnedPlayerTuning().Engaged());
    EXPECT_EQ(mods::OwnedPlayerTuning().Value(Tuning::RunSpeed), 1);
}
} // namespace
