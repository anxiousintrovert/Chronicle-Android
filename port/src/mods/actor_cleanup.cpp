#include "actor_cleanup.hpp"
#include "entity_tracking.hpp"
#include "entity_events.hpp"
#include "entity_reactions.hpp"
#include "owned_enemy_stats.hpp"
#include "owned_enemy_scale.hpp"
#include "retail_sound.hpp"
#include "monstorunit.hpp"
#include "dataalloc.hpp"
#include "chronicle/monster_limits.hpp"
#include "collisiondata.hpp"
#include "shot_effect_pack.hpp"
#include <iterator>
#include <type_traits>

namespace mods {
ActorCombatCleanup ClearActorCombatReferences(CCollisionData *collision, CSHOT_EFFECT_PACK *shots, unsigned slot) noexcept {
    ActorCombatCleanup result;
    if (slot >= chronicle::monsters::SlotCapacity) return result;
    const int owner = static_cast<int>(slot) * 5 + 200;
    if (collision) {
        static_assert(std::extent_v<decltype(CCollisionData::active)> == std::extent_v<decltype(CCollisionData::hit)>);
        for (unsigned i = 0; i < std::size(collision->active); ++i) {
            if (collision->active[i] && (collision->hit[i].owner == owner || collision->hit[i].monster_no == static_cast<int>(slot))) {
                collision->active[i] = 0; ++result.hits;
            }
        }
    }
    if (shots) for (auto &effect : shots->effect) {
        static_assert(std::extent_v<decltype(CSHOT_EFFECT::active)> == std::extent_v<decltype(CSHOT_EFFECT::user_id_2)>);
        for (unsigned i = 0; i < std::size(effect.active); ++i)
            if (effect.active[i] && effect.user_id_2[i] == static_cast<int>(slot)) {
                effect.OffEffect(static_cast<int>(i)); ++result.projectiles;
            }
    }
    return result;
}
}

namespace mods {
bool RetireOwnedActor(CMonstorUnit *unit, ActorOwners &owners, EntityHandles::Handle handle,
                      std::string_view owner, CCollisionData *collision, CSHOT_EFFECT_PACK *shots,
                      int basic, int chapter, int voice_table, std::string &error) {
    auto *handles = TrackedEntities(unit);
    if (!handles || !owners.CheckOwner(*handles, handle, owner, error)) {
        if (!handles) error = "actor unit is not currently tracked";
        return false;
    }
    const auto slot = handles->Resolve(handle);
    if (!slot || *slot >= unit->SlotLimit()) { error = "actor slot is unavailable"; return false; }
    auto &actor = unit->monster[*slot];
    if ((actor.state != 1 && actor.state != 2) || actor.hp <= 0 || unit->alive_count <= 0) {
        error = "actor is dead, transitioning or has invalid alive accounting"; return false;
    }
    if (actor.attachment_kind == 8 || actor.kind == MONSTER_KIND_MIMIC_SMALL || actor.kind == MONSTER_KIND_MIMIC_LARGE || actor.revealed == 0) {
        error = "mimic retirement requires chest-trigger cleanup"; return false;
    }
    if (actor.event_flag2 >= 0 || actor.event_flag2_pending) { error = "event-linked actor cannot be retired"; return false; }
    // The only allocating commit preparation happens before native mutations.
    // This also rejects invalid table identities before cancelling attacks.
    if (!RetireActorSoundSequence(unit->sound[*slot].sequence_id, *slot, basic, chapter, voice_table, error)) return false;
    ClearActorCombatReferences(collision, shots, *slot);
    ClearOwnedEnemyScales(unit, static_cast<int>(*slot));
    ClearOwnedEnemyStats(unit, static_cast<int>(*slot));
    ClearEntityReactions(unit, static_cast<int>(*slot));
    EnemyDeaths().CancelEntity(unit, handle);
    // Reset the retired slot to reusable clean-floor defaults. Keep model,
    // character and script allocations owned by the floor; do not free them.
    actor = {};
    actor.state = -1; actor.falls = true;
    actor.body_radius = actor.collision_radius = 13.0f;
    actor.drop_item = actor.shot_effect = actor.shot_effect2 = actor.last_hit_damage = -1;
    actor.clip_distance = 300.0f; actor.shadow_length = 1.0f;
    actor.lockon_scale_x = actor.lockon_scale_y = 1.0f;
    actor.lock_range = 120.0f; actor.lockon_enabled = true;
    actor.revealed = actor.steal_item = actor.event_flag2 = -1;
    actor.palette_alpha = 128.0f;
    actor.shadow_visible = actor.shadow_enabled = true;
    actor.knockback_direction[3] = actor.knockback[2] = 1.0f;
    unit->script_state[*slot] = 0;
    if (unit->script[*slot]) unit->script[*slot]->Reset();
    unit->effect[*slot] = {}; unit->effect2[*slot] = {}; unit->effect3[*slot] = {};
    unit->event[*slot] = {}; unit->event2[*slot] = {}; unit->sound[*slot] = {};
    for (auto &flag : unit->event_flags[*slot]) flag = 0;
    for (auto &id : unit->sound[*slot].id) id = -1;
    unit->sound[*slot].sequence_id = -1;
    --unit->alive_count;
    handles->Retire(*slot); owners.Forget(handle);
    error.clear(); return true;
}
}
