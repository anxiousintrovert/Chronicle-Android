#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include <limits>
#include "mods/camera.hpp"
#include "mods/api.hpp"
#include "camerafollow.hpp"
#include "dun/gameloop.hpp"
#include "gamemode.hpp"
#include "main.hpp"
#include "savedata.hpp"
extern s32 mode;
extern s32 gameTask, NextMapNo, existFlag;
extern CDungeonMap MainDungeonMap;
namespace {
TEST(ModCamera, LiveAdapterReleasesStoppedOwnersAndRejectsMixedWrites) {
    auto save = std::make_unique<CSaveData>();
    const auto old_mode = mode, old_task = gameTask, old_next = NextMapNo, old_exist = existFlag;
    const auto old_game_mode = BtGameModeFlag, old_event = BtEventMode;
    auto *old_save = SaveData; auto *old_status = UserStatus; auto *old_camera = NowCamera__3;
    auto *old_map = NowDngMap;
    const float distance = MainCamera__4.GetDistance(), height = MainCamera__4.GetHeight(), angle = MainCamera__4.next_angle;
    const auto following = MainCamera__4.follow_on;
    struct Cleanup {
        std::function<void()> fn;
        ~Cleanup() { fn(); }
    } cleanup{[&] {
        mods::ReleaseDungeonCamera();
        MainCamera__4.SetDistance(distance); MainCamera__4.SetHeight(height); MainCamera__4.SetAngle(angle);
        MainCamera__4.follow_on = following;
        mode = old_mode; gameTask = old_task; NextMapNo = old_next; existFlag = old_exist;
        BtGameModeFlag = old_game_mode; BtEventMode = old_event;
        SaveData = old_save; UserStatus = old_status; NowCamera__3 = old_camera; NowDngMap = old_map;
    }};
    SaveData = save.get(); UserStatus = save->GetDngStatus(); NowCamera__3 = &MainCamera__4;
    // Adapter only checks map identity here; it never dereferences the map.
    NowDngMap = &MainDungeonMap;
    mode = GAME_MODE_DUNGEON; gameTask = GAME_TASK_PLAY; BtGameModeFlag = BT_GAME_MODE_PLAY;
    NextMapNo = -1; existFlag = 0; BtEventMode = 0;
    MainCamera__4.SetDistance(60); MainCamera__4.SetHeight(0); MainCamera__4.SetAngle(0); MainCamera__4.FollowOn();
    std::string error;
    std::vector<mods::GameCommand> writes{{"render.camera_set", {{"distance", 85}}, "test.camera"}};
    ASSERT_TRUE(mods::ApplyDungeonCamera(writes, 0, error)) << error;
    EXPECT_EQ(MainCamera__4.GetDistance(), 85);
    EXPECT_FALSE(mods::ApplyDungeonCamera(writes, 1, error));
    writes.push_back({"player.heal", {{"amount", 1}}, "test.camera"});
    EXPECT_FALSE(mods::ApplyDungeonCamera(writes, 0, error));
    EXPECT_EQ(MainCamera__4.GetDistance(), 85);
    // The synthetic owner is not running; retirement must restore its baseline.
    mods::RetainDungeonCamera();
    EXPECT_EQ(MainCamera__4.GetDistance(), 60);
    writes.pop_back();
    ASSERT_TRUE(mods::ApplyDungeonCamera(writes, 0, error));
    mods::ReleaseDungeonCamera(); // The boundary used before item-camera copies.
    CCameraFollow copy{1, 1, 0, 1};
    copy = MainCamera__4;
    EXPECT_EQ(copy.GetDistance(), 60);
    MainCamera__4.SetDistance(100); // A newly initialized scene.
    mods::RetainDungeonCamera();
    EXPECT_EQ(MainCamera__4.GetDistance(), 100);
}
TEST(ModCamera, NativeSnapshotIsDetachedAndPreservesCameraAndRng) {
    CCameraFollow camera{85, 25, 0, 1};
    camera.pos[0] = 11; camera.pos[1] = 22; camera.pos[2] = 33;
    camera.ref[0] = -4; camera.ref[1] = 5; camera.ref[2] = 6;
    camera.angle_h = 0.25f; camera.angle_v = -0.5f; camera.roll = 0.125f;
    camera.SetDistance(85); camera.SetHeight(25); camera.SetAngle(2.25f); camera.FollowOn();
    std::array<unsigned char, sizeof(camera)> before{};
    std::memcpy(before.data(), &camera, sizeof(camera));
    srand(456); const int expected = rand(); srand(456);
    auto snapshot = mods::ReadCameraSnapshot(camera);
    EXPECT_EQ(snapshot["eye"], nlohmann::json::array({11, 22, 33}));
    EXPECT_EQ(snapshot["target"], nlohmann::json::array({-4, 5, 6}));
    EXPECT_EQ(snapshot["heading"], 0.25f); EXPECT_EQ(snapshot["pitch"], -0.5f);
    EXPECT_EQ(snapshot["roll"], 0.125f); EXPECT_EQ(snapshot["angle"], 2.25f);
    EXPECT_EQ(snapshot["distance"], 85); EXPECT_EQ(snapshot["height"], 25);
    EXPECT_EQ(snapshot["following"], true);
    snapshot["eye"][0] = 999;
    EXPECT_EQ(std::memcmp(before.data(), &camera, sizeof(camera)), 0);
    EXPECT_EQ(rand(), expected);
}
TEST(ModCamera, RejectsInvalidPoseAndUnavailableScene) {
    CCameraFollow camera{85, 25, 0, 1};
    camera.pos[0] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW(mods::ReadCameraSnapshot(camera), std::runtime_error);
    camera.pos[0] = 0; camera.next_angle = std::numeric_limits<float>::infinity();
    EXPECT_THROW(mods::ReadCameraSnapshot(camera), std::runtime_error);
    const auto old_mode = mode; auto *old_camera = NowCamera__3;
    mode = GAME_MODE_EDIT; NowCamera__3 = &camera;
    EXPECT_THROW(mods::ReadDungeonCamera(), std::runtime_error);
    mode = GAME_MODE_DUNGEON; NowCamera__3 = nullptr;
    EXPECT_THROW(mods::ReadDungeonCamera(), std::runtime_error);
    mode = old_mode; NowCamera__3 = old_camera;
    EXPECT_TRUE(mods::ValidateOperation("render.camera_get", nlohmann::json::object()).empty());
    EXPECT_FALSE(mods::ValidateOperation("render.camera_get", {{"unknown", 1}}).empty());
}
}
