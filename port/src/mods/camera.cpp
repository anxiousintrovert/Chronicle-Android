#include "camera.hpp"
#include <cmath>
#include "camerafollow.hpp"
#include "dun/gameloop.hpp"
#include "gamemode.hpp"
#include "main.hpp"

extern s32 mode;
namespace mods {
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
    return ReadCameraSnapshot(*NowCamera__3);
}
}
