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
extern s32 mode;
namespace {
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
