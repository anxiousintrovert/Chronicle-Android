#include "actor_spawn.hpp"
#include "monstorunit.hpp"
#include "dataalloc.hpp"
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
