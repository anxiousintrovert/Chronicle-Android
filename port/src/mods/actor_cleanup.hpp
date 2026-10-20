#pragma once
#include "owned_actors.hpp"
class CMonstorUnit;
class CCollisionData;
class CSHOT_EFFECT_PACK;
namespace mods {
// Internal retirement; caller supplies current scene and sound-table context.
// Rejects non-owned, stale, dying, mimic and event-linked actors before mutation.
bool RetireOwnedActor(CMonstorUnit *unit, ActorOwners &owners, EntityHandles::Handle handle,
                      std::string_view owner, CCollisionData *collision, CSHOT_EFFECT_PACK *shots,
                      int basic, int chapter, int voice_table, std::string &error);
struct ActorCombatCleanup { unsigned hits = 0, projectiles = 0; };
// Commit-only helper. Caller must validate live actor ownership and scene first.
// Does not retire the actor, award kills, stop sound, or touch other slots.
ActorCombatCleanup ClearActorCombatReferences(CCollisionData *collision, CSHOT_EFFECT_PACK *shots, unsigned slot) noexcept;
}
