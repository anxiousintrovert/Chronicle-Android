#include "entity_tracking.hpp"

#include "entity_events.hpp"
#include "owned_actors.hpp"
#include <stdexcept>
#include "entity_reactions.hpp"
#include "monstorunit.hpp"
#include "owned_enemy_scale.hpp"
#include "owned_enemy_stats.hpp"
#include <type_traits>

namespace mods {
namespace {
// Fail compilation if paired native storage arrays drift apart.
// Inner per-monster collision/sound dimensions are deliberately independent.
static_assert(EntityHandles::Capacity <= chronicle::monsters::StorageCapacity);
static_assert(std::extent_v<decltype(CMonstorUnit::monster)> == chronicle::monsters::StorageCapacity);
static_assert(std::extent_v<decltype(CMonstorUnit::script)> == chronicle::monsters::StorageCapacity);
static_assert(std::extent_v<decltype(CMonstorUnit::script_state)> == chronicle::monsters::StorageCapacity);
static_assert(std::extent_v<decltype(CMonstorUnit::chara)> == chronicle::monsters::StorageCapacity);
static_assert(std::extent_v<decltype(CMonstorUnit::interpreter)> == chronicle::monsters::StorageCapacity);
static_assert(std::extent_v<decltype(CMonstorUnit::effect)> == chronicle::monsters::StorageCapacity);
static_assert(std::extent_v<decltype(CMonstorUnit::effect2)> == chronicle::monsters::StorageCapacity);
static_assert(std::extent_v<decltype(CMonstorUnit::effect3)> == chronicle::monsters::StorageCapacity);
static_assert(std::extent_v<decltype(CMonstorUnit::sound)> == chronicle::monsters::StorageCapacity);
static_assert(std::extent_v<decltype(CMonstorUnit::event)> == chronicle::monsters::StorageCapacity);
static_assert(std::extent_v<decltype(CMonstorUnit::event2)> == chronicle::monsters::StorageCapacity);
static_assert(std::extent_v<decltype(CMonstorUnit::guard)> == chronicle::monsters::StorageCapacity);
static_assert(std::extent_v<decltype(CMonstorUnit::event_flags)> == chronicle::monsters::StorageCapacity);
EntityHandles handles;
CMonstorUnit *tracked = nullptr;
} // namespace

EntityHandles &TrackEntities(CMonstorUnit &unit) {
    if (tracked != &unit) {
        if (HasBoundActorOwnership(tracked))
            throw std::runtime_error("owned actors must be cleaned up before switching identity tracking");
        handles.Deactivate();
        tracked = &unit;
        handles.Activate();
        for (unsigned i = 0; i < unit.SlotLimit(); ++i) {
            if (unit.monster[i].state != -1) {
                handles.Construct(i);
            }
        }
    }
    return handles;
}

EntityHandles *TrackedEntities(CMonstorUnit *unit) { return unit && tracked == unit ? &handles : nullptr; }

void StopTrackingEntities() {
    if (!tracked || HasBoundActorOwnership(tracked)) {
        return;
    }
    tracked = nullptr;
    handles.Deactivate();
}

void ExpireEntitiesOutsideUnit(CMonstorUnit *unit) {
    if (tracked && tracked != unit) {
        StopTrackingEntities();
    }
}

void EntityUnitCleared(CMonstorUnit *unit) {
    ClearBoundActorOwnership(unit);
    ClearOwnedEnemyScales(unit);
    ClearOwnedEnemyStats(unit);
    EnemyDeaths().CancelUnit(unit);
    ClearEntityReactions(unit);
    if (tracked == unit) {
        handles.Clear();
    }
}

void EntitySlotConstructed(CMonstorUnit *unit, unsigned slot) {
    if (!unit || slot >= unit->SlotLimit()) return;
    ClearOwnedEnemyScales(unit, static_cast<int>(slot));
    ClearOwnedEnemyStats(unit, static_cast<int>(slot));
    ClearEntityReactions(unit, static_cast<int>(slot));
    if (tracked == unit) {
        ForgetBoundActorOwnership(unit, handles.Get(slot));
        handles.Construct(slot);
    }
}
} // namespace mods
