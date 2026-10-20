#pragma once
class CCollisionData;
class CSHOT_EFFECT_PACK;
namespace mods {
struct ActorCombatCleanup { unsigned hits = 0, projectiles = 0; };
// Commit-only helper. Caller must validate live actor ownership and scene first.
// Does not retire the actor, award kills, stop sound, or touch other slots.
ActorCombatCleanup ClearActorCombatReferences(CCollisionData *collision, CSHOT_EFFECT_PACK *shots, unsigned slot) noexcept;
}
