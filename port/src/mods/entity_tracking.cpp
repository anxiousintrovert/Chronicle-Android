#include "entity_tracking.hpp"

#include "entity_events.hpp"
#include "entity_reactions.hpp"
#include "monstorunit.hpp"

namespace mods {
namespace {
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
    EnemyDeaths().CancelUnit(unit);
    ClearEntityReactions(unit);
    if (tracked == unit) {
        handles.Clear();
    }
}

void EntitySlotConstructed(CMonstorUnit *unit, unsigned slot) {
    ClearEntityReactions(unit, static_cast<int>(slot));
    if (tracked == unit) {
        handles.Construct(slot);
    }
}
} // namespace mods
