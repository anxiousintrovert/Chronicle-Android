#pragma once
#include "runtime.hpp"
class CCameraFollow;
namespace mods {
// Detached native snapshot shared by the live host and focused tests.
nlohmann::json ReadCameraSnapshot(CCameraFollow &camera);
nlohmann::json ReadDungeonCamera();
bool ApplyDungeonCamera(const std::vector<GameCommand> &commands, int money, std::string &error);
void RetainDungeonCamera();
// Before native camera initialization/copy/takeover and at game-run exit.
void ReleaseDungeonCamera();
}
