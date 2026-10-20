#include <gtest/gtest.h>
#include <algorithm>
#include "mods/lighting.hpp"
#include "chronicle/mod_lighting.hpp"
#include "dun/gameloop.hpp"
#include "dungeonmap.hpp"
#include "main.hpp"
#include "savedata.hpp"
extern s32 mode, gameTask, NextMapNo, existFlag;
extern CDungeonMap MainDungeonMap, UraDungeonMap;
namespace {
TEST(ModLightingLive, BoundMapRetirementAndStoppedOwnersRestoreWithoutCrossingScenes) {
    auto save = std::make_unique<CSaveData>();
    const auto old_mode = mode, old_task = gameTask, old_next = NextMapNo, old_exist = existFlag;
    const auto old_game_mode = BtGameModeFlag, old_event = BtEventMode;
    auto *old_save = SaveData; auto *old_status = UserStatus; auto *old_map = NowDngMap;
    const float old_scale = MainDungeonMap.draw_dist_scale;
    std::array<u8, 3> old_background{};
    std::copy_n(main_bgColor, 3, old_background.begin());
    struct Cleanup { std::function<void()> fn; ~Cleanup() { fn(); } } cleanup{[&] {
        mods::ReleaseDungeonLighting();
        MainDungeonMap.draw_dist_scale = old_scale;
        std::copy(old_background.begin(), old_background.end(), main_bgColor);
        mode = old_mode; gameTask = old_task; NextMapNo = old_next; existFlag = old_exist;
        BtGameModeFlag = old_game_mode; BtEventMode = old_event;
        SaveData = old_save; UserStatus = old_status; NowDngMap = old_map;
    }};
    SaveData = save.get(); UserStatus = save->GetDngStatus(); NowDngMap = &MainDungeonMap;
    mode = GAME_MODE_DUNGEON; gameTask = GAME_TASK_PLAY; BtGameModeFlag = BT_GAME_MODE_PLAY;
    NextMapNo = -1; existFlag = 0; BtEventMode = 0;
    MainDungeonMap.draw_dist_scale = 4;
    std::string error;
    std::vector<mods::GameCommand> writes{{"dungeon.lighting_set",
        {{"side", "front"}, {"background", {20, 30, 40}}, {"view_scale", 2}}, "test.lighting"}};
    ASSERT_TRUE(mods::ApplyDungeonLighting(writes, 0, error)) << error;
    EXPECT_EQ(main_bgColor[1], 30); EXPECT_EQ(MainDungeonMap.draw_dist_scale, 2);
    EXPECT_FALSE(mods::ApplyDungeonLighting(writes, 1, error));
    mods::ReleaseDungeonLightingForMap(&UraDungeonMap);
    EXPECT_EQ(MainDungeonMap.draw_dist_scale, 2);
    mods::ReleaseDungeonLightingForMap(&MainDungeonMap);
    EXPECT_EQ(MainDungeonMap.draw_dist_scale, 4);
    EXPECT_TRUE(std::equal(old_background.begin(), old_background.end(), main_bgColor));
    ASSERT_TRUE(mods::ApplyDungeonLighting(writes, 0, error)) << error;
    mods::RetainDungeonLighting(); // Synthetic owner is absent from runtime.
    EXPECT_EQ(MainDungeonMap.draw_dist_scale, 4);
    ASSERT_TRUE(mods::ApplyDungeonLighting(writes, 0, error)) << error;
    mods::ReleaseDungeonLighting(); // Must precede model-definition loading.
    MainDungeonMap.draw_dist_scale = 6; main_bgColor[1] = 77;
    mods::RetainDungeonLighting();
    EXPECT_EQ(MainDungeonMap.draw_dist_scale, 6); EXPECT_EQ(main_bgColor[1], 77);
    EXPECT_TRUE(mods::ReadDungeonLighting()["owners"].empty());
}
}
