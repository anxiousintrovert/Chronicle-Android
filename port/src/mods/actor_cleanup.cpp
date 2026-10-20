#include "actor_cleanup.hpp"
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
