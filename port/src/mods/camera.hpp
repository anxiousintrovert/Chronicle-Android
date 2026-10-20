#pragma once
#include "runtime.hpp"
class CCameraFollow;
namespace mods {
// Detached native snapshot shared by the live host and focused tests.
nlohmann::json ReadCameraSnapshot(CCameraFollow &camera);
nlohmann::json ReadDungeonCamera();
}
