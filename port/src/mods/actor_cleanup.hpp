#pragma once
#include "owned_actors.hpp"
#include <span>
class CMonstorUnit;
class CCollisionData;
class CSHOT_EFFECT_PACK;
namespace mods {
// Internal retirement; caller supplies current scene and sound-table context.
// Rejects non-owned, stale, dying, mimic and event-linked actors before mutation.
bool RetireOwnedActor(CMonstorUnit *unit, ActorOwners &owners, EntityHandles::Handle handle,
                      std::string_view owner, CCollisionData *collision, CSHOT_EFFECT_PACK *shots,
                      int basic, int chapter, int voice_table, std::string &error);
struct ActorRetirementFailure { EntityHandles::Handle entity; std::string owner, reason; };
struct ActorRetentionResult {
    unsigned retained = 0, retired = 0;
    std::vector<ActorRetirementFailure> pending;
    bool Complete() const { return pending.empty(); }
};
// Best-effort owner cleanup, not an atomic batch. Successful removals stay
// committed; failures keep ownership and are returned for a later retry. Call
// before stopping identity tracking, with this unit's current native context.
// Explicit native floor-clear/slot-construction notifications remain responsible
// for discarding records whose native lifetime is known to have ended.
ActorRetentionResult RetainOwnedActors(CMonstorUnit *unit, ActorOwners &owners,
    std::span<const std::string> active_owners, CCollisionData *collision,
    CSHOT_EFFECT_PACK *shots, int basic, int chapter, int voice_table);
struct ActorCombatCleanup { unsigned hits = 0, projectiles = 0; };
// Commit-only helper. Caller must validate live actor ownership and scene first.
// Does not retire the actor, award kills, stop sound, or touch other slots.
ActorCombatCleanup ClearActorCombatReferences(CCollisionData *collision, CSHOT_EFFECT_PACK *shots, unsigned slot) noexcept;
}
