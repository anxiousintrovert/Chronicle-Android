#pragma once
#include <array>
#include <optional>
#include <string>
#include <span>
#include <nlohmann/json.hpp>
#include "owned_actors.hpp"
class CDungeonMap;
struct ITEM_FREE_AREA;
class CMonstorUnit;
namespace mods {
nlohmann::json ReadLoadedActorModels(const CMonstorUnit *unit);
std::span<const ITEM_FREE_AREA> LoadedActorPlacementAreas(int dungeon);
bool ValidateActorPlacement(const CDungeonMap &map, std::span<const ITEM_FREE_AREA> areas,
                            std::array<float, 3> position, float radius, std::string &error);
bool ValidateActorObstructions(const CDungeonMap &map, CMonstorUnit &unit,
                               std::array<float, 3> position, float radius,
                               std::array<float, 3> player_position, float player_radius, std::string &error);
struct ActorSpawnTuning {
    std::optional<int> max_hp, defense, reward_exp, reward_money;
    std::optional<std::array<float, 3>> rotation;
};
// Parse optional stats/rotation without changing output on failure.
std::string ParseActorSpawnTuning(const nlohmann::json &arguments, ActorSpawnTuning &output);
std::string ValidateActorSpawnTuning(const ActorSpawnTuning &tuning);
struct LoadedActorSpawnPlan { int slot; int model; std::array<float, 3> position; };
// Read-only prerequisites for a loaded retail model. This does not validate a
// custom compiled script, floor geometry, or gameplay transition readiness.
std::optional<LoadedActorSpawnPlan> PlanLoadedActorSpawn(const CMonstorUnit *unit, int model,
                                                       std::array<float, 3> position, std::string &error);
// Commit-only native creation, for a stable dungeon on the game thread. The
// caller must already track this unit and must not batch other writes with it.
// Loaded retail initialization scripts run normally; this is not a sandbox for
// arbitrary bytecode or a rollback boundary for native script side effects.
std::optional<EntityHandles::Handle> CreateLoadedActor(
    CMonstorUnit &unit, const CDungeonMap &map, std::span<const ITEM_FREE_AREA> areas,
    ActorOwners &owners, std::string_view owner, std::string_view definition,
    int model, std::array<float, 3> position, std::array<float, 3> player_position,
    float player_radius, std::string &error, const ActorSpawnTuning &tuning = {});
}
