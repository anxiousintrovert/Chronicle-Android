#include "camera.hpp"
#include <cmath>
#include "camerafollow.hpp"
#include "dun/gameloop.hpp"
#include "gamemode.hpp"
#include "main.hpp"
#include "camera_orbit.hpp"
#include "savedata.hpp"
#include <algorithm>

extern s32 mode, gameTask, NextMapNo, existFlag;
namespace mods {
namespace {
CameraOrbit orbit;
std::uint64_t orbit_epoch = 0;
int orbit_dungeon = -1, orbit_floor = -1;
const void *orbit_map = nullptr;
std::string Unavailable() {
    if (mode != GAME_MODE_DUNGEON || gameTask != GAME_TASK_PLAY ||
        BtGameModeFlag != BT_GAME_MODE_PLAY || BtEventMode || NextMapNo >= 0 || existFlag)
        return "camera writes require stable dungeon play outside events and transitions";
    if (!SaveData || UserStatus != static_cast<CUserStatus *>(SaveData->GetDngStatus()) || !NowDngMap ||
        NowCamera__3 != &MainCamera__4 || !MainCamera__4.follow_on)
        return "the primary native dungeon follow camera is unavailable";
    return {};
}
}
nlohmann::json ReadCameraSnapshot(CCameraFollow &camera) {
    float eye[4]{}, target[4]{};
    camera.GetPos(eye);
    camera.GetRef(target);
    const float heading = camera.GetAngleH(), pitch = camera.angle_v, roll = camera.GetRoll();
    const float distance = camera.GetDistance(), height = camera.GetHeight(), angle = camera.next_angle;
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(eye[axis]) || !std::isfinite(target[axis]))
            throw std::runtime_error("native dungeon camera contains a non-finite position");
    }
    for (const float value : {heading, pitch, roll, distance, height, angle}) {
        if (!std::isfinite(value)) throw std::runtime_error("native dungeon camera contains a non-finite orbit or angle");
    }
    return {{"scene", "dungeon"}, {"eye", {eye[0], eye[1], eye[2]}},
            {"target", {target[0], target[1], target[2]}}, {"heading", heading},
            {"pitch", pitch}, {"roll", roll}, {"distance", distance}, {"height", height},
            {"angle", angle}, {"following", camera.follow_on != 0}};
}
nlohmann::json ReadDungeonCamera() {
    if (mode != GAME_MODE_DUNGEON || !NowCamera__3)
        throw std::runtime_error("there is no active dungeon camera");
    auto result = ReadCameraSnapshot(*NowCamera__3);
    const auto reason = Unavailable();
    result["writable"] = reason.empty();
    result["reason"] = reason;
    result["owners"] = orbit.Owners();
    return result;
}
void ReleaseDungeonCamera() {
    // The primary camera is a process-lifetime global, never an arena pointer.
    if (!orbit.Active()) return;
    orbit.Retain(MainCamera__4, {});
    orbit.Forget();
}
void RetainDungeonCamera() {
    if (!orbit.Active()) return;
    if (!Unavailable().empty() || orbit_epoch != Scripts().Epoch() ||
        orbit_dungeon != UserStatus->cur_georama || orbit_floor != UserStatus->cur_floor ||
        orbit_map != NowDngMap) {
        ReleaseDungeonCamera();
        return;
    }
    orbit.Retain(MainCamera__4, Scripts().ActiveOwners());
}
bool ApplyDungeonCamera(const std::vector<GameCommand> &commands, int money, std::string &error) {
    if (money || commands.empty() || std::ranges::any_of(commands, [](const auto &command) {
            return command.operation != "render.camera_set" && command.operation != "render.camera_reset";
        })) {
        error = "camera writes cannot share a callback with other engine writes, including currency";
        return false;
    }
    error = Unavailable();
    if (!error.empty()) return false;
    if (!orbit.Apply(MainCamera__4, commands, error)) return false;
    orbit_epoch = Scripts().Epoch();
    orbit_dungeon = UserStatus->cur_georama;
    orbit_floor = UserStatus->cur_floor;
    orbit_map = NowDngMap;
    return true;
}
}
