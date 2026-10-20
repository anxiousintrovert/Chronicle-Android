#include "actor_spawn.hpp"
#include "monstorunit.hpp"
#include "dataalloc.hpp"
#include "entity_tracking.hpp"
#include "userstatus.hpp"
#include "dun/gameloop.hpp"
#include <stdexcept>
#include <cmath>
#include <iterator>

namespace mods {
std::optional<LoadedActorSpawnPlan> PlanLoadedActorSpawn(const CMonstorUnit *unit, int model,
                                                       std::array<float, 3> position, std::string &error) {
    const auto fail = [&](const char *reason) -> std::optional<LoadedActorSpawnPlan> { error = reason; return {}; };
    if (!unit) return fail("dungeon monster unit unavailable");
    for (float value : position)
        if (!std::isfinite(value) || std::abs(value) > 1000000.0f) return fail("spawn position must be finite and bounded");
    if (unit->model_count < 0 || unit->model_count > static_cast<int>(std::size(unit->model)) ||
        model < 0 || model >= unit->model_count) return fail("monster model is not loaded on this floor");
    const auto &definition = unit->model[model];
    if (definition.attachment_kind == 8 || definition.kind == MONSTER_KIND_MIMIC_SMALL || definition.kind == MONSTER_KIND_MIMIC_LARGE)
        return fail("mimic spawning requires a separate chest-trigger lifecycle");
    if (!unit->script_data[model]) return fail("loaded monster script unavailable");
    if (!unit->base_chara[model][0].frame) return fail("loaded monster model frame unavailable");
    for (int part = 1; part < 4; ++part)
        if (definition.model_name[part][0] && !unit->base_chara[model][part].frame)
            return fail("loaded monster attachment frame unavailable");
    int slot = -1;
    for (int i = 0; i < unit->SlotLimit(); ++i) if (unit->monster[i].state == -1) { slot = i; break; }
    if (slot < 0) return fail("no free slot within the active monster limit");
    // SetupViewMonstor chooses the first free slot too. Never skip an unready
    // first slot and accidentally validate a different arena than native uses.
    const auto *arena = unit->script[slot];
    // SetupViewMonstor resets the arena; BtSetEventScript reserves 64 + 384 quadwords.
    if (!arena || !arena->base || arena->limit < 448)
        return fail("first free monster slot has insufficient script storage");
    error.clear(); return LoadedActorSpawnPlan{slot, model, position};
}
}

namespace mods {
std::optional<EntityHandles::Handle> CreateLoadedActor(
    CMonstorUnit &unit, const CDungeonMap &map, std::span<const ITEM_FREE_AREA> areas,
    ActorOwners &owners, std::string_view owner, std::string_view definition,
    int model, std::array<float, 3> position, std::array<float, 3> player_position,
    float player_radius, std::string &error) {
    const auto fail = [&](const char *reason) -> std::optional<EntityHandles::Handle> { error = reason; return {}; };
    auto *handles = TrackedEntities(&unit);
    if (!handles) return fail("actor identity tracking is unavailable");
    const auto plan = PlanLoadedActorSpawn(&unit, model, position, error);
    if (!plan) return {};
    if (!handles->CanConstruct(plan->slot)) return fail("actor identity generations exhausted");
    if (!UserStatus || NowMonstorUnit != &unit || NowDngMap != &map)
        return fail("native dungeon context does not match actor creation");
    // This native slot invokes boss-specific frame initialization. Generic
    // spawning must not enter that special encounter path.
    if (UserStatus->cur_georama == 3 && UserStatus->cur_floor == 17 && plan->slot == 1)
        return fail("actor slot is reserved for native boss initialization");
    if (unit.alive_count < 0 || unit.alive_count >= unit.SlotLimit() || unit.model[model].max_hp <= 0)
        return fail("invalid live monster accounting or model health");
    const float radius = unit.model[model].collision_radius;
    if (!ValidateActorPlacement(map, areas, position, radius, error) ||
        !ValidateActorObstructions(map, unit, position, radius, player_position, player_radius, error)) return {};
    auto reservation = owners.Reserve(*handles, owner, definition, error);
    if (!reservation) return {};
    // No mod callback is delivered inside native setup. Slot selection and the
    // construction notification therefore use the preflighted slot/registry.
    if (!unit.SetupViewMonstor(model, position.data(), -1)) return fail("native monster creation rejected");
    const auto handle = handles->Get(plan->slot);
    if (!owners.Publish(*handles, handle, std::move(*reservation)))
        throw std::logic_error("native actor creation violated its ownership reservation");
    error.clear(); return handle;
}
}
