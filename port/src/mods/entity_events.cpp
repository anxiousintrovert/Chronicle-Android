#include "entity_events.hpp"

#include "dun/gameloop.hpp"
#include "entity_tracking.hpp"
#include "monstorunit.hpp"
#include "userstatus.hpp"

namespace mods {
namespace {
EnemyDeathQueue deaths;
}

EnemyDeathQueue &EnemyDeaths() { return deaths; }

void CaptureEnemyDeath(CMonstorUnit *unit, int slot, bool scripted) {
    if (!deaths.Enabled()) {
        return;
    }
    if (!unit || slot < 0 || static_cast<unsigned>(slot) >= unit->SlotLimit() || !UserStatus || UserStatus->cur_georama < 0 ||
        UserStatus->cur_georama >= 6 || UserStatus->cur_floor < 0 || UserStatus->cur_floor >= 100) {
        return;
    }
    const auto &actor = unit->monster[slot];
    const auto  handle = TrackEntities(*unit).Get(static_cast<unsigned>(slot));
    if (!handle) {
        return;
    }
    deaths.Push({handle, UserStatus->cur_georama, UserStatus->cur_floor, actor.last_attacker,
                 actor.exp, actor.money, actor.last_hit_damage, scripted, unit});
}
} // namespace mods
