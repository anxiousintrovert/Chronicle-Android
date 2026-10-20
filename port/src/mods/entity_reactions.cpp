#include "entity_reactions.hpp"

#include <array>
#include <cassert>

#include "dngstatusdata.hpp"
#include "dun/gameloop.hpp"
#include "entity_events.hpp"
#include "monstorunit.hpp"
#include "chronicle/monster_limits.hpp"

namespace mods {
namespace {
struct Reaction {
    CMonstorUnit *unit = nullptr;
    unsigned      slot = 0;
    int           result = MONSTER_DMG_NONE;
};

std::array<Reaction, 2 * chronicle::monsters::SlotCapacity> pending;
unsigned                 count = 0;
} // namespace

bool CanQueueEntityReactions(CMonstorUnit *unit) {
    if (!unit) {
        return false;
    }
    unsigned available = 0;
    for (const auto &entry : pending) {
        if (!entry.unit || entry.unit == unit) {
            ++available;
        }
    }
    return available >= chronicle::monsters::SlotCapacity;
}

void QueueEntityReaction(CMonstorUnit *unit, unsigned slot, int result) noexcept {
    assert(unit && slot < chronicle::monsters::SlotCapacity && (result == MONSTER_DMG_HIT || result == MONSTER_DMG_KILLED));
    for (auto &entry : pending) {
        if (entry.unit == unit && entry.slot == slot) {
            if (result == MONSTER_DMG_KILLED) {
                entry.result = result;
            }
            return;
        }
    }
    for (auto &entry : pending) {
        if (!entry.unit) {
            entry = {unit, slot, result};
            ++count;
            return;
        }
    }
    // Caller must preflight capacity before committing native fields.
    assert(false && "entity reaction capacity was not reserved");
}

int TakeEntityReaction(CMonstorUnit *unit, unsigned slot) noexcept {
    if (!count) {
        return MONSTER_DMG_NONE;
    }
    for (auto &entry : pending) {
        if (entry.unit == unit && entry.slot == slot) {
            const int result = entry.result;
            entry = {};
            --count;
            return result;
        }
    }
    return MONSTER_DMG_NONE;
}

void ClearEntityReactions(CMonstorUnit *unit, int slot) noexcept {
    if (!count) {
        return;
    }
    for (auto &entry : pending) {
        if (entry.unit == unit && (slot < 0 || entry.slot == static_cast<unsigned>(slot))) {
            entry = {};
            --count;
        }
    }
}

int ConsumeEntityDamage(CMonstorUnit *unit, int slot) noexcept {
    if (!count || !unit || slot < 0 || static_cast<unsigned>(slot) >= chronicle::monsters::SlotCapacity) {
        return MONSTER_DMG_NONE;
    }
    const int result = TakeEntityReaction(unit, static_cast<unsigned>(slot));
    if (result != MONSTER_DMG_KILLED) {
        return result;
    }
    auto &actor = unit->monster[slot];
    if (actor.stop_timer > 0) {
        actor.stop_timer = 0;
        actor.motion_reset_pending = true;
    }
    actor.hp = 0;
    actor.last_attacker = -1;
    if (unit->alive_count > 0) {
        --unit->alive_count;
    }
    if (UserStatus && UserStatus->cur_georama >= 0 && UserStatus->cur_georama < 6 &&
        UserStatus->cur_floor >= 0 && UserStatus->cur_floor < 100 &&
        UserStatus->kills[UserStatus->cur_georama][UserStatus->cur_floor] < 32767) {
        static_cast<CDngStatusData *>(UserStatus)->AddKills();
    }
    if (actor.event_flag2 != -1) {
        actor.event_flag2_pending = 1;
    }
    CaptureEnemyDeath(unit, slot, true);
    return result;
}
} // namespace mods
