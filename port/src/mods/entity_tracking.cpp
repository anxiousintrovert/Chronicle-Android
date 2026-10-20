#include "entity_tracking.hpp"

#include "entity_events.hpp"
#include "entity_reactions.hpp"
#include "monstorunit.hpp"
#include "owned_enemy_scale.hpp"
#include "owned_enemy_stats.hpp"
#include <type_traits>

namespace mods {
namespace {
// Fail compilation if native storage and framework indexing drift apart.
// Inner per-monster collision/sound dimensions are deliberately independent.
static_assert(std::extent_v<decltype(CMonstorUnit::monster)> == EntityHandles::Capacity);
static_assert(std::extent_v<decltype(CMonstorUnit::script)> == EntityHandles::Capacity);
static_assert(std::extent_v<decltype(CMonstorUnit::script_state)> == EntityHandles::Capacity);
static_assert(std::extent_v<decltype(CMonstorUnit::chara)> == EntityHandles::Capacity);
static_assert(std::extent_v<decltype(CMonstorUnit::interpreter)> == EntityHandles::Capacity);
static_assert(std::extent_v<decltype(CMonstorUnit::effect)> == EntityHandles::Capacity);
static_assert(std::extent_v<decltype(CMonstorUnit::effect2)> == EntityHandles::Capacity);
static_assert(std::extent_v<decltype(CMonstorUnit::effect3)> == EntityHandles::Capacity);
static_assert(std::extent_v<decltype(CMonstorUnit::sound)> == EntityHandles::Capacity);
static_assert(std::extent_v<decltype(CMonstorUnit::event)> == EntityHandles::Capacity);
static_assert(std::extent_v<decltype(CMonstorUnit::event2)> == EntityHandles::Capacity);
static_assert(std::extent_v<decltype(CMonstorUnit::guard)> == EntityHandles::Capacity);
static_assert(std::extent_v<decltype(CMonstorUnit::event_flags)> == EntityHandles::Capacity);
EntityHandles handles;
CMonstorUnit *tracked = nullptr;
} // namespace

EntityHandles &TrackEntities(CMonstorUnit &unit) {
    if (tracked != &unit) {
        handles.Deactivate();
        tracked = &unit;
        handles.Activate();
        for (unsigned i = 0; i < EntityHandles::Capacity; ++i) {
            if (unit.monster[i].state != -1) {
                handles.Construct(i);
            }
        }
    }
    return handles;
}

void StopTrackingEntities() {
    if (!tracked) {
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
    ClearOwnedEnemyScales(unit);
    ClearOwnedEnemyStats(unit);
    EnemyDeaths().CancelUnit(unit);
    ClearEntityReactions(unit);
    if (tracked == unit) {
        handles.Clear();
    }
}

void EntitySlotConstructed(CMonstorUnit *unit, unsigned slot) {
    ClearOwnedEnemyScales(unit, static_cast<int>(slot));
    ClearOwnedEnemyStats(unit, static_cast<int>(slot));
    ClearEntityReactions(unit, static_cast<int>(slot));
    if (tracked == unit) {
        handles.Construct(slot);
    }
}
} // namespace mods
