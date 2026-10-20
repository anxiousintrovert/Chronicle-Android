#pragma once
#include <array>
#include <optional>
#include <string>
class CMonstorUnit;
namespace mods {
struct LoadedActorSpawnPlan { int slot; int model; std::array<float, 3> position; };
// Read-only prerequisites for a loaded retail model. This does not validate a
// custom compiled script, floor geometry, or gameplay transition readiness.
std::optional<LoadedActorSpawnPlan> PlanLoadedActorSpawn(const CMonstorUnit *unit, int model,
                                                       std::array<float, 3> position, std::string &error);
}
