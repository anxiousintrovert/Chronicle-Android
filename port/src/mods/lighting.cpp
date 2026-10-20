#include "lighting.hpp"
#include <algorithm>
#include <cmath>
#include "dun/gameloop.hpp"
#include "dungeonmap.hpp"
#include "main.hpp"
#include "savedata.hpp"
#include "lighting_control.hpp"
#include "chronicle/mod_lighting.hpp"
extern s32 mode, lightingMode, gameTask, NextMapNo, existFlag;
extern CDungeonMap MainDungeonMap, UraDungeonMap;

namespace mods {
namespace {
using Json = nlohmann::json;
// Native global constructors may initialize maps before this translation unit.
// This constant-initialized gate keeps those calls entirely dormant.
constinit bool engaged = false;
CDungeonMap *bound_map = nullptr;
std::uint64_t bound_epoch = 0;
int bound_dungeon = -1, bound_floor = -1;
LightingControl &Control() { static LightingControl control; return control; }
std::string Unavailable() {
    if (mode != GAME_MODE_DUNGEON || gameTask != GAME_TASK_PLAY || BtGameModeFlag != BT_GAME_MODE_PLAY ||
        BtEventMode || NextMapNo >= 0 || existFlag)
        return "lighting writes require stable dungeon play outside events and transitions";
    if (!SaveData || UserStatus != static_cast<CUserStatus *>(SaveData->GetDngStatus()) ||
        (NowDngMap != &MainDungeonMap && NowDngMap != &UraDungeonMap))
        return "native dungeon save and map are unavailable";
    return {};
}
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
LightingState CaptureState(const CDungeonMap &map) {
    LightingState state;
    state.active = lightingMode; state.view_scale = map.draw_dist_scale;
    state.sides[0] = Capture(main_fogRate, main_fogColor, main_bgColor, main_ambientlight, main_light, main_lightcolor);
    state.sides[1] = Capture(sub_fogRate, sub_fogColor, sub_bgColor, sub_ambientlight, sub_light, sub_lightcolor);
    return state;
}
void PublishSide(const LightingSide &before, const LightingSide &after, float *fog, u8 *fog_color,
                 u8 *background, float *ambient, sceVu0FMATRIX direction, sceVu0FMATRIX color) noexcept {
    if (before.fog != after.fog) std::copy(after.fog.begin(), after.fog.end(), fog);
    if (before.fog_color != after.fog_color) std::copy(after.fog_color.begin(), after.fog_color.end(), fog_color);
    if (before.background != after.background) std::copy(after.background.begin(), after.background.end(), background);
    if (before.ambient != after.ambient) std::copy(after.ambient.begin(), after.ambient.end(), ambient);
    for (int light = 0; light < 3; ++light) for (int axis = 0; axis < 3; ++axis) {
        if (before.direction[light][axis] != after.direction[light][axis]) direction[axis][light] = after.direction[light][axis];
        if (before.color[light][axis] != after.color[light][axis]) color[light][axis] = after.color[light][axis];
    }
}
void Publish(const LightingState &before, const LightingState &after, CDungeonMap &map) noexcept {
    PublishSide(before.sides[0], after.sides[0], main_fogRate, main_fogColor, main_bgColor, main_ambientlight, main_light, main_lightcolor);
    PublishSide(before.sides[1], after.sides[1], sub_fogRate, sub_fogColor, sub_bgColor, sub_ambientlight, sub_light, sub_lightcolor);
    if (before.view_scale != after.view_scale) map.draw_dist_scale = after.view_scale;
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
    auto result = ReadLightingSnapshot(CaptureState(*NowDngMap));
    const auto reason = Unavailable();
    result["writable"] = reason.empty(); result["reason"] = reason;
    result["owners"] = engaged ? Control().Owners() : Json::object();
    return result;
}
void ReleaseDungeonLighting() noexcept {
    if (!engaged) return;
    try {
        const auto before = CaptureState(*bound_map);
        auto after = before;
        Control().Retain(after, {});
        Publish(before, after, *bound_map);
    } catch (...) {
        // Never unwind through a native loader. Invalid native data is left
        // untouched rather than publishing a partially restored snapshot.
    }
    Control().Forget(); engaged = false; bound_map = nullptr;
}
void ReleaseDungeonLightingForMap(const void *map) noexcept {
    if (engaged && bound_map == map) ReleaseDungeonLighting();
}
void RetainDungeonLighting() noexcept {
    if (!engaged) return;
    try {
        if (!Unavailable().empty() || bound_epoch != Scripts().Epoch() || bound_map != NowDngMap ||
            bound_dungeon != UserStatus->cur_georama || bound_floor != UserStatus->cur_floor) {
            ReleaseDungeonLighting(); return;
        }
        const auto before = CaptureState(*bound_map);
        auto after = before;
        Control().Retain(after, Scripts().ActiveOwners());
        Publish(before, after, *bound_map);
        engaged = Control().Active();
        if (!engaged) bound_map = nullptr;
    } catch (...) { ReleaseDungeonLighting(); }
}
bool ApplyDungeonLighting(const std::vector<GameCommand> &commands, int money, std::string &error) {
    if (money || commands.empty() || std::ranges::any_of(commands, [](const auto &command) {
            return command.operation != "dungeon.lighting_set" && command.operation != "dungeon.lighting_reset";
        })) {
        error = "lighting cannot share a callback with other engine writes, including currency"; return false;
    }
    error = Unavailable();
    if (!error.empty()) return false;
    const auto before = CaptureState(*NowDngMap);
    auto after = before;
    if (!Control().Apply(after, commands, error)) return false;
    Publish(before, after, *NowDngMap);
    engaged = Control().Active(); bound_map = engaged ? NowDngMap : nullptr;
    bound_epoch = Scripts().Epoch(); bound_dungeon = UserStatus->cur_georama; bound_floor = UserStatus->cur_floor;
    return true;
}
}
