#include "owned_enemy_stats.hpp"

#include <algorithm>
#include <cstdint>

#include "monstorunit.hpp"

namespace mods {
EnemyStatOwners &OwnedEnemyStats() {
    static EnemyStatOwners owners;
    return owners;
}

EnemyStatValues ReadEnemyStats(const MONSTOR &a) {
    return {a.hp, a.max_hp, a.defense, a.exp, a.money, a.drop_item};
}

void WriteEnemyStats(MONSTOR &a, const EnemyStatValues &v) noexcept {
    a.hp = v[0];
    a.max_hp = v[1];
    a.defense = static_cast<short>(v[2]);
    a.exp = v[3];
    a.money = v[4];
    a.drop_item = static_cast<short>(v[5]);
}

int ScaleEnemyHealth(int hp, int old_max, int new_max) {
    if (hp <= 0 || old_max <= 0 || new_max <= 0) {
        return hp;
    }
    return static_cast<int>(std::clamp<std::int64_t>(
        static_cast<std::int64_t>(hp) * new_max / old_max, 1, new_max));
}

void ReleaseEnemyStats(EnemyStatClaims &c, EnemyStatValues &v, std::string_view owner) {
    const int  old_hp = v[0], old_max = v[1];
    const bool restore_max = c[1].owner == owner && !owner.empty() && v[1] == c[1].applied;
    const bool restore_hp = c[0].owner == owner && !owner.empty() && v[0] == c[0].applied;
    for (unsigned i = 0; i < c.size(); ++i) {
        v[i] = c[i].Release(owner, v[i]);
    }
    // Damage taken while a larger maximum was in effect remains proportional;
    // zero HP never becomes positive during cleanup.
    if (old_hp <= 0) {
        v[0] = old_hp;
    } else if (restore_max && !restore_hp) {
        v[0] = ScaleEnemyHealth(old_hp, old_max, v[1]);
    }
    if ((restore_hp || restore_max) && v[0] > 0 && v[1] > 0) {
        v[0] = std::min(v[0], v[1]);
    }
}

namespace {
bool Empty(const std::array<EnemyStatClaims, 16> &slots) {
    for (const auto &claims : slots) {
        for (const auto &claim : claims) {
            if (!claim.owner.empty()) {
                return false;
            }
        }
    }
    return true;
}

void ReleaseSlot(CMonstorUnit &unit, unsigned slot, EnemyStatClaims &claims,
                 std::span<const std::string> active) {
    auto values = ReadEnemyStats(unit.monster[slot]);
    bool changed = false;
    for (const auto &claim : claims) {
        if (!claim.owner.empty() && std::ranges::find(active, claim.owner) == active.end()) {
            // Release clears its claim, so do not retain a view into that string.
            const auto owner = claim.owner;
            ReleaseEnemyStats(claims, values, owner);
            changed = true;
        }
    }
    if (changed) {
        WriteEnemyStats(unit.monster[slot], values);
    }
}
} // namespace

void PruneOwnedEnemyStats(EnemyStatOwners &owners) {
    std::erase_if(owners, [](const auto &entry) { return Empty(entry.second); });
}

void ClearOwnedEnemyStats(CMonstorUnit *unit, int slot) {
    auto      &owners = OwnedEnemyStats();
    const auto it = owners.find(unit);
    if (it == owners.end()) {
        return;
    }
    for (unsigned i = 0; i < it->second.size(); ++i) {
        if (slot < 0 || i == static_cast<unsigned>(slot)) {
            ReleaseSlot(*unit, i, it->second[i], {});
        }
    }
    if (Empty(it->second)) {
        owners.erase(it);
    }
}

void RetainOwnedEnemyStats(std::span<const std::string> active) {
    auto &owners = OwnedEnemyStats();
    for (auto it = owners.begin(); it != owners.end();) {
        for (unsigned i = 0; i < it->second.size(); ++i) {
            ReleaseSlot(*it->first, i, it->second[i], active);
        }
        if (Empty(it->second)) {
            it = owners.erase(it);
        } else {
            ++it;
        }
    }
}
} // namespace mods
