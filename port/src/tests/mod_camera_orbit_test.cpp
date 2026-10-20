#include <gtest/gtest.h>
#include "mods/camera_orbit.hpp"
#include "camerafollow.hpp"

namespace {
TEST(ModCameraOrbit, ConflictingBatchLeavesNativeValuesAndOwnershipUnchanged) {
    CCameraFollow camera{60, 0, 0, 1};
    camera.FollowOn();
    mods::CameraOrbit orbit;
    std::string error;
    const std::vector<mods::GameCommand> first{{"render.camera_set", {{"distance", 85}}, "first"}};
    ASSERT_TRUE(orbit.Apply(camera, first, error)) << error;
    const auto owners = orbit.Owners();
    const std::vector<mods::GameCommand> conflict{
        {"render.camera_set", {{"height", 25}}, "second"},
        {"render.camera_set", {{"distance", 95}}, "second"}};
    EXPECT_FALSE(orbit.Apply(camera, conflict, error));
    EXPECT_NE(error.find("first"), std::string::npos);
    EXPECT_EQ(camera.GetDistance(), 85); EXPECT_EQ(camera.GetHeight(), 0);
    EXPECT_EQ(orbit.Owners(), owners);
}
TEST(ModCameraOrbit, DisjointClaimsResetAndReleaseConditionallyRestoreBaselines) {
    CCameraFollow camera{60, 0, 0, 1};
    camera.FollowOn();
    mods::CameraOrbit orbit;
    std::string error;
    const std::vector<mods::GameCommand> writes{
        {"render.camera_set", {{"distance", 85}, {"angle", 9}}, "first"},
        {"render.camera_set", {{"height", 25}}, "second"},
        {"render.camera_set", {{"distance", 95}}, "first"}};
    ASSERT_TRUE(orbit.Apply(camera, writes, error)) << error;
    EXPECT_EQ(camera.GetDistance(), 95); EXPECT_EQ(camera.GetHeight(), 25);
    EXPECT_LT(camera.next_angle, 3.142f);
    camera.SetHeight(30); // Native camera behavior supersedes this mod's value.
    const std::vector<mods::GameCommand> reset{{"render.camera_reset", nlohmann::json::object(), "first"}};
    ASSERT_TRUE(orbit.Apply(camera, reset, error)) << error;
    EXPECT_EQ(camera.GetDistance(), 60);
    orbit.Retain(camera, {});
    EXPECT_EQ(camera.GetHeight(), 30);
    EXPECT_FALSE(orbit.Active());
}
TEST(ModCameraOrbit, DormantAndSceneDiscardPathsDoNotTouchNativeCamera) {
    CCameraFollow camera{60, 0, 0, 1};
    camera.FollowOn();
    mods::CameraOrbit orbit;
    std::string error;
    srand(425); const int expected = rand(); srand(425);
    ASSERT_TRUE(orbit.Apply(camera, {}, error));
    orbit.Retain(camera, {});
    EXPECT_EQ(camera.GetDistance(), 60); EXPECT_EQ(rand(), expected);
    const std::vector<mods::GameCommand> writes{{"render.camera_set", {{"distance", 85}}, "first"}};
    ASSERT_TRUE(orbit.Apply(camera, writes, error));
    orbit.Forget(); // Before native camera reinitialization, discard old identity.
    camera.SetDistance(100);
    orbit.Retain(camera, {});
    EXPECT_EQ(camera.GetDistance(), 100);
    EXPECT_FALSE(orbit.Active());
    camera.FollowOff();
    EXPECT_FALSE(orbit.Apply(camera, writes, error));
    EXPECT_EQ(camera.GetDistance(), 100);
    EXPECT_FALSE(mods::CameraOrbit::Validate("render.camera_set", {{"distance", 301}}).empty());
    EXPECT_FALSE(mods::CameraOrbit::Validate("render.camera_reset", {{"distance", 60}}).empty());
}
}
