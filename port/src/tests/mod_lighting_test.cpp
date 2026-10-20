#include <gtest/gtest.h>
#include <cstring>
#include <limits>
#include "mods/lighting.hpp"
#include "mods/api.hpp"
#include "dun/gameloop.hpp"
#include "dungeonmap.hpp"
#include "main.hpp"
extern s32 mode, lightingMode;
namespace {
TEST(ModLighting, SnapshotPreservesBothSidesAndRng) {
    mods::LightingState state;
    state.active = 1; state.view_scale = 4;
    state.sides[0].fog = {1000, 3500, 0, 255};
    state.sides[0].fog_color = {5, 6, 7};
    state.sides[0].background = {8, 9, 10};
    state.sides[1].ambient = {11, 12, 13};
    state.sides[1].direction[2] = {0.5f, 0.25f, -1};
    state.sides[1].color[2] = {14, 15, 16};
    const auto before = state;
    srand(998); const int expected = rand(); srand(998);
    auto result = mods::ReadLightingSnapshot(state);
    EXPECT_EQ(result["side"], "back");
    EXPECT_EQ(result["view_scale"], 4);
    EXPECT_EQ(result["front"]["fog"]["far"], 3500);
    EXPECT_EQ(result["front"]["fog"]["color"], nlohmann::json::array({5, 6, 7}));
    EXPECT_EQ(result["back"]["ambient"], nlohmann::json::array({11, 12, 13}));
    EXPECT_EQ(result["back"]["lights"][2]["direction"][1], 0.25f);
    EXPECT_EQ(result["back"]["lights"][2]["color"][0], 14);
    result["front"]["fog"]["far"] = 1;
    EXPECT_EQ(std::memcmp(&state, &before, sizeof(state)), 0);
    EXPECT_EQ(rand(), expected);
    state.sides[0].direction[0][0] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW(mods::ReadLightingSnapshot(state), std::runtime_error);
    EXPECT_TRUE(mods::ValidateOperation("dungeon.lighting_get", nlohmann::json::object()).empty());
    EXPECT_FALSE(mods::ValidateOperation("dungeon.lighting_get", {{"side", "front"}}).empty());
}
TEST(ModLighting, LiveMappingUsesDirectionColumnsAndColorRows) {
    const auto old_mode = mode, old_side = lightingMode;
    auto *old_map = NowDngMap;
    const float old_direction = main_light[1][2], old_color = main_lightcolor[2][1];
    auto map = std::make_unique<CDungeonMap>();
    struct Cleanup { std::function<void()> fn; ~Cleanup() { fn(); } } cleanup{[&] {
        mode = old_mode; lightingMode = old_side; NowDngMap = old_map;
        main_light[1][2] = old_direction; main_lightcolor[2][1] = old_color;
    }};
    mode = GAME_MODE_DUNGEON; lightingMode = 0; NowDngMap = map.get();
    map->draw_dist_scale = 3.5f; main_light[1][2] = 0.75f; main_lightcolor[2][1] = 142;
    auto result = mods::ReadDungeonLighting();
    EXPECT_EQ(result["front"]["lights"][2]["direction"][1], 0.75f);
    EXPECT_EQ(result["front"]["lights"][2]["color"][1], 142);
    EXPECT_EQ(result["view_scale"], 3.5f);
    NowDngMap = nullptr;
    EXPECT_THROW(mods::ReadDungeonLighting(), std::runtime_error);
    NowDngMap = map.get(); mode = GAME_MODE_EDIT;
    EXPECT_THROW(mods::ReadDungeonLighting(), std::runtime_error);
}
}
