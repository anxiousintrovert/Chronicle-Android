#include "lighting.hpp"
#include <algorithm>
#include <cmath>
#include "dun/gameloop.hpp"
#include "dungeonmap.hpp"
#include "main.hpp"
extern s32 mode, lightingMode;

namespace mods {
namespace {
using Json = nlohmann::json;
float Finite(float value) {
    if (!std::isfinite(value)) throw std::runtime_error("native lighting contains a non-finite value");
    return value;
}
Json Side(const LightingSide &side) {
    Json lights = Json::array();
    for (int i = 0; i < 3; ++i) {
        lights.push_back({{"light", i}, {"direction", {Finite(side.direction[i][0]), Finite(side.direction[i][1]), Finite(side.direction[i][2])}},
                          {"color", {Finite(side.color[i][0]), Finite(side.color[i][1]), Finite(side.color[i][2])}}});
    }
    return {{"fog", {{"near", Finite(side.fog[0])}, {"far", Finite(side.fog[1])},
                      {"min", Finite(side.fog[2])}, {"max", Finite(side.fog[3])}, {"color", side.fog_color}}},
            {"background", side.background},
            {"ambient", {Finite(side.ambient[0]), Finite(side.ambient[1]), Finite(side.ambient[2])}},
            {"lights", std::move(lights)}};
}
LightingSide Capture(const float *fog, const u8 *fog_color, const u8 *background, const float *ambient,
                     const sceVu0FMATRIX direction, const sceVu0FMATRIX color) {
    LightingSide result;
    std::copy_n(fog, 4, result.fog.begin());
    std::copy_n(fog_color, 3, result.fog_color.begin());
    std::copy_n(background, 3, result.background.begin());
    std::copy_n(ambient, 3, result.ambient.begin());
    for (int light = 0; light < 3; ++light) for (int axis = 0; axis < 3; ++axis) {
        // Native directions are columns; light colors are rows.
        result.direction[light][axis] = direction[axis][light];
        result.color[light][axis] = color[light][axis];
    }
    return result;
}
}
Json ReadLightingSnapshot(const LightingState &state) {
    return {{"side", state.active == 0 ? "front" : "back"},
            {"front", Side(state.sides[0])}, {"back", Side(state.sides[1])},
            {"view_scale", Finite(state.view_scale)}};
}
Json ReadDungeonLighting() {
    if (mode != GAME_MODE_DUNGEON || !NowDngMap)
        throw std::runtime_error("lighting requires an active dungeon map");
    LightingState state;
    state.active = lightingMode;
    state.view_scale = NowDngMap->draw_dist_scale;
    state.sides[0] = Capture(main_fogRate, main_fogColor, main_bgColor, main_ambientlight, main_light, main_lightcolor);
    state.sides[1] = Capture(sub_fogRate, sub_fogColor, sub_bgColor, sub_ambientlight, sub_light, sub_lightcolor);
    return ReadLightingSnapshot(state);
}
}
