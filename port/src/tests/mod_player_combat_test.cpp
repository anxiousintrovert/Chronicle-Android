#include <gtest/gtest.h>

#include <cstring>
#include <memory>

#include "btactstatus.hpp"
#include "character.hpp"
#include "dun/gameloop.hpp"
#include "framevu1.hpp"
#include "main.hpp"
#include "mods/api.hpp"
#include "mods/entities.hpp"
#include "mods/player_combat.hpp"
#include "mods/player_tuning_native.hpp"
#include "monstorunit.hpp"
#include "savedata.hpp"

extern s32 mode;

namespace {
class ModPlayerCombat : public testing::Test {
protected:
    int                           old_mode = mode, old_draw = BtAllDrawFlag;
    CSaveData                    *old_save = SaveData;
    CUserStatus                  *old_user = UserStatus;
    CMonstorUnit                 *old_unit = NowMonstorUnit;
    CFrame                       *old_frame = CharaMain.frame;
    BT_ACT_STATUS                 old_battle = BtActStatus;
    std::unique_ptr<CSaveData>    save = std::make_unique<CSaveData>();
    std::unique_ptr<CMonstorUnit> unit = std::make_unique<CMonstorUnit>();
    CFrameVu1                     frame;
    std::string                   error;

    void SetUp() override {
        SaveData = save.get();
        UserStatus = save->GetDngStatus();
        NowMonstorUnit = unit.get();
        CharaMain.frame = &frame;
        BtAllDrawFlag = 1;
        mode = GAME_MODE_DUNGEON;
        BtActStatus = {};
        UserStatus->party_size = 1;
        UserStatus->cur_chara = 0;
        UserStatus->money = 10;
    }

    void TearDown() override {
        mode = old_mode;
        BtAllDrawFlag = old_draw;
        SaveData = old_save;
        UserStatus = old_user;
        NowMonstorUnit = old_unit;
        CharaMain.frame = old_frame;
        BtActStatus = old_battle;
    }

    mods::GameCommand Grant(int frames) { return {"player.invincible", {{"frames", frames}}, "test.combat"}; }

    bool Apply(std::vector<mods::GameCommand> commands, int money = 0) {
        return mods::ApplyEntityAndSave(*save, unit.get(), commands, money, error, nullptr, nullptr);
    }
};

TEST_F(ModPlayerCombat, ExactSchemaPermissionAndBoundaries) {
    const auto *operation = mods::FindOperation("player.invincible");
    ASSERT_NE(operation, nullptr);
    EXPECT_TRUE(operation->writes);
    EXPECT_EQ(operation->permission, "game.write");
    for (int frames : {1, 600}) {
        EXPECT_TRUE(mods::ValidateOperation("player.invincible", {
                                                                     {"frames", frames}
        })
                        .empty());
    }
    for (nlohmann::json frames : {nlohmann::json(0), nlohmann::json(601), nlohmann::json(-1), nlohmann::json(1.0), nlohmann::json(true)}) {
        EXPECT_FALSE(mods::ValidateOperation("player.invincible", {
                                                                      {"frames", frames}
        })
                         .empty());
    }
    EXPECT_FALSE(mods::ValidateOperation("player.invincible", {
                                                                  {"frames",    1},
                                                                  {"character", 0}
    })
                     .empty());
    EXPECT_FALSE(mods::ValidateOperation("player.invincible", {
                                                                  {"frames", 1      },
                                                                  {"owner",  "spoof"}
    })
                     .empty());
    EXPECT_FALSE(mods::ValidateOperation("player.combat_state", {
                                                                    {"frames", 1}
    })
                     .empty());
    EXPECT_FALSE(Apply({
        {"player.invincible", {{"frames", 30}}, ""}
    }));
    EXPECT_EQ(BtActStatus.invincible_frames, 0);
}

TEST_F(ModPlayerCombat, DetachedReadsAndEmptyTransactionsPreserveNativeBytesAndRng) {
    const auto before = BtActStatus;
    srand(301);
    const int expected = rand();
    srand(301);
    const auto state = mods::ReadPlayerCombat();
    EXPECT_TRUE(state["ready"]);
    EXPECT_EQ(state["character"], 0);
    EXPECT_EQ(state["invincible_frames"], 0);
    ASSERT_TRUE(Apply({}));
    EXPECT_EQ(std::memcmp(&BtActStatus, &before, sizeof(before)), 0);
    EXPECT_EQ(rand(), expected);
}

TEST_F(ModPlayerCombat, RejectsWrongSceneMissingResourcesAndMismatchedSave) {
    mode = GAME_MODE_EDIT;
    EXPECT_FALSE(Apply({Grant(30)}));
    EXPECT_FALSE(mods::ReadPlayerCombat()["ready"]);
    EXPECT_FALSE(mods::ReadPlayerCombat().contains("invincible_frames"));
    mode = GAME_MODE_DUNGEON;
    UserStatus = nullptr;
    EXPECT_FALSE(Apply({Grant(30)}));
    UserStatus = save->GetDngStatus();
    CharaMain.frame = nullptr;
    EXPECT_FALSE(Apply({Grant(30)}));
    CharaMain.frame = &frame;
    BtAllDrawFlag = 0;
    EXPECT_FALSE(Apply({Grant(30)}));
    BtAllDrawFlag = 1;
    UserStatus->cur_chara = 1;
    EXPECT_FALSE(Apply({Grant(30)}));
    UserStatus->cur_chara = 0;
    auto other = std::make_unique<CSaveData>();
    EXPECT_FALSE(mods::ApplyEntityAndSave(*other, unit.get(), {Grant(30)}, 0, error, nullptr, nullptr));
    EXPECT_EQ(BtActStatus.invincible_frames, 0);
}

TEST_F(ModPlayerCombat, RequestsOnlyExtendTimerAndDoNotChangeOtherCombatOrSaveFields) {
    BtActStatus.invincible_frames = 700;
    const auto before = BtActStatus;
    ASSERT_TRUE(Apply({Grant(1), Grant(600)}));
    EXPECT_EQ(std::memcmp(&BtActStatus, &before, sizeof(before)), 0);
    BtActStatus.invincible_frames = 10;
    ASSERT_TRUE(Apply({Grant(120), Grant(30)}, 5));
    EXPECT_EQ(BtActStatus.invincible_frames, 120);
    EXPECT_EQ(UserStatus->money, 15);
    EXPECT_EQ(BtActStatus.recoil_frames, before.recoil_frames);
    EXPECT_EQ(BtActStatus.action_gauge, before.action_gauge);
}

TEST_F(ModPlayerCombat, FailedMixedWritesAndInvalidEntitiesCannotLeakTimerOrMoney) {
    BtActStatus.invincible_frames = 7;
    EXPECT_FALSE(Apply({
                           Grant(600), {"unsupported.write", {}, "test.combat"}
    },
                       5));
    EXPECT_EQ(BtActStatus.invincible_frames, 7);
    EXPECT_EQ(UserStatus->money, 10);
    EXPECT_FALSE(Apply({
                           Grant(600), {"entities.set_stats", {{"entity", "999999999"}, {"attack", 3}}, "test.combat"}
    },
                       5));
    EXPECT_EQ(BtActStatus.invincible_frames, 7);
    EXPECT_EQ(UserStatus->money, 10);
    const auto tuning_before = mods::OwnedPlayerTuning().State();
    EXPECT_FALSE(Apply({
                           Grant(600), {"player.tuning_set", {{"run_speed", 2}}, "test.combat"},
                            {"unsupported.write", {},                 "test.combat"}
    },
                       5));
    EXPECT_EQ(BtActStatus.invincible_frames, 7);
    EXPECT_EQ(UserStatus->money, 10);
    EXPECT_EQ(mods::OwnedPlayerTuning().State(), tuning_before);
    EXPECT_FALSE(Apply({
                           Grant(600), {"shops.set_items", {{"shop", -1}, {"items", nlohmann::json::array()}}, "test.combat"}
    },
                       5));
    EXPECT_EQ(BtActStatus.invincible_frames, 7);
    EXPECT_EQ(UserStatus->money, 10);
}

TEST_F(ModPlayerCombat, StagingIsDetachedCommitIsOneShotAndNeverShortensLaterNativeTimer) {
    mods::PlayerCombatTransaction transaction;
    const std::array              batch{Grant(60)};
    ASSERT_TRUE(transaction.Stage(*save, batch, error));
    EXPECT_EQ(BtActStatus.invincible_frames, 0);
    BtActStatus.invincible_frames = 90;
    transaction.Commit();
    EXPECT_EQ(BtActStatus.invincible_frames, 90);
    BtActStatus.invincible_frames = 0;
    transaction.Commit();
    EXPECT_EQ(BtActStatus.invincible_frames, 0);
    ASSERT_TRUE(transaction.Stage(*save, batch, error));
    const std::array invalid{Grant(601)};
    EXPECT_FALSE(transaction.Stage(*save, invalid, error));
    transaction.Commit();
    EXPECT_EQ(BtActStatus.invincible_frames, 0);
}
} // namespace
