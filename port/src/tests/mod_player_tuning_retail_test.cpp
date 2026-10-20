#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <fstream>

#include "btactstatus.hpp"
#include "character.hpp"
#include "dun/gameloop.hpp"
#include "gameloop.hpp"
#include "gfx_fixture.hpp"
#include "mods/player_tuning_native.hpp"
#include "platform/clock.hpp"
#include "platform/config.hpp"
#include "platform/paths.hpp"
#include "userstatus.hpp"

void motionDrive();

namespace {
TEST(ModPlayerTuningRetail, NativeGaugeAndSwingRecoveryRespectRetailWindows) {
    const char *data = std::getenv("DC_DATA");
    const char *audit = std::getenv("DC_PLAYER_TUNING_AUDIT");
    if (!data || !audit || !*audit) {
        GTEST_SKIP() << "Run alone with DC_DATA and DC_PLAYER_TUNING_AUDIT (disposable directory)";
    }
    const std::filesystem::path root = audit;
    std::filesystem::create_directories(root);
    PathsSetDataRoot(data);
    PathsSetSaveRoot(root);
    ConfigLoad();
    dc::test::GfxFixture graphics(640, 480, 1.0f, dc::test::GfxOptions{.offscreen = true});
    ClockSetUnbounded(true);
    ClockReset();
    ASSERT_TRUE(GameSetJump("dungeon:0"));
    GameSetFastLoad(true);
    GameSetAutosave(false);
    GameSetFrameBudget(90);
    ASSERT_EQ(RunGame(0, nullptr), 0);
    ASSERT_NE(UserStatus, nullptr);
    ASSERT_NE(NowWeaponHave, nullptr);
    ASSERT_NE(CharaMain.motion_type.motion_info, nullptr);
    ASSERT_EQ(UserStatus->cur_chara, CHARA_TOAN);
    auto report = nlohmann::json::array();
    auto set = [&](nlohmann::json args) {
        const std::array<mods::GameCommand, 1> batch{{{"player.tuning_set", std::move(args), "audit.tuning"}}};
        std::string                            error;
        EXPECT_TRUE(mods::OwnedPlayerTuning().Stage(batch, 1, error)) << error;
    };
    UserStatus->ailments[0] = 0;
    NowWeaponHave->speed = 60;
    BtAllDrawFlag = 1;
    for (float scale : {0.25f, 1.0f, 4.0f}) {
        set({
            {"gauge_refill", scale}
        });
        BtActStatus.action_gauge = 10;
        BtActStatus.swinging = 0;
        motionDrive();
        EXPECT_FLOAT_EQ(BtActStatus.action_gauge, 10 + 0.5f * scale);
        report.push_back({
            {"gauge_scale", scale                   },
            {"gauge",       BtActStatus.action_gauge}
        });
    }
    constexpr float last_window[5] = {828, 835, 844, 854, 868};
    for (int action = 0x24; action <= 0x28; ++action) {
        auto &info = CharaMain.motion_type.motion_info[action];
        int   tuned_steps = 0;
        BtActStatus.action_on = 1;
        BtActStatus.action_no = BtActStatus.motion_no = action;
        for (float scale : {0.25f, 1.0f, 4.0f}) {
            set({
                {"attack_anim_speed", scale}
            });
            for (float time : {last_window[action - 0x24], last_window[action - 0x24] + 0.5f, float(info.end) - 2}) {
                ASSERT_GE(time, info.start);
                ASSERT_LT(time, info.end);
                CharaMain.SetMotion(action, 0);
                auto &state = CharaMain.motion_type.state;
                state.motion_no = state.playing_no = action;
                state.time = time;
                state.frame = static_cast<unsigned>(std::floor(time));
                state.next_frame = state.frame + 1;
                state.blend = 1;
                state.blending = 0;
                CharaMain.motion_speed = -1;
                const bool  tail = time > last_window[action - 0x24] && time < info.end - 2;
                const float expected = tail && scale != 1 ? std::min(info.end - 1.5f - time, info.speed * scale) : info.speed;
                if (expected != info.speed) {
                    ++tuned_steps;
                }
                mods::StepPlayerMotion();
                EXPECT_FLOAT_EQ(state.time, time + expected) << "action=" << action << " scale=" << scale << " time=" << time;
                EXPECT_EQ(CharaMain.motion_speed, -1);
                report.push_back({
                    {"action", action    },
                    {"scale",  scale     },
                    {"from",   time      },
                    {"to",     state.time}
                });
            }
        }
        if (action == 0x24) {
            EXPECT_EQ(tuned_steps, 0);
        } else {
            EXPECT_GT(tuned_steps, 0);
        }
    }
    mods::OwnedPlayerTuning().Retain({}, 1);
    EXPECT_FALSE(mods::OwnedPlayerTuning().Engaged());
    std::ofstream(root / "results.json") << report.dump(2);
}
} // namespace
