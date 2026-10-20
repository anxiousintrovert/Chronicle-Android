#pragma once
#include "entity_handles.hpp"
class CMonstorUnit;

namespace mods {
// Only the explicitly observed dungeon unit is tracked. Switching units expires
// all old handles, even if the old unit remains allocated for the back floor.
// Bound owned actors pin tracking until cleanup or a native lifetime notification.
// Switching to another unit while pinned rejects; Stop preserves the registry.
EntityHandles &TrackEntities(CMonstorUnit &unit);
// Read-only lookup; does not switch units or activate tracking.
EntityHandles *TrackedEntities(CMonstorUnit *unit);
void           StopTrackingEntities();
void           ExpireEntitiesOutsideUnit(CMonstorUnit *unit);
void           EntityUnitCleared(CMonstorUnit *unit);
void           EntitySlotConstructed(CMonstorUnit *unit, unsigned slot);
} // namespace mods
