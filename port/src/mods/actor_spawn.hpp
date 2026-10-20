#pragma once
#include <array>
#include <optional>
#include <string>
#include <span>
class CDungeonMap;
struct ITEM_FREE_AREA;
class CMonstorUnit;
namespace mods {
std::span<const ITEM_FREE_AREA> LoadedActorPlacementAreas(int dungeon);
bool ValidateActorPlacement(const CDungeonMap &map, std::span<const ITEM_FREE_AREA> areas,
                            std::array<float, 3> position, float radius, std::string &error);
struct LoadedActorSpawnPlan { int slot; int model; std::array<float, 3> position; };
// Read-only prerequisites for a loaded retail model. This does not validate a
// custom compiled script, floor geometry, or gameplay transition readiness.
std::optional<LoadedActorSpawnPlan> PlanLoadedActorSpawn(const CMonstorUnit *unit, int model,
                                                       std::array<float, 3> position, std::string &error);
}
