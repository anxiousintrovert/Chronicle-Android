#include "owned_enemy_scale.hpp"

#include <algorithm>
#include <cmath>

#include "monstorunit.hpp"

namespace mods {
EnemyScale ReadEnemyScale(const CCharacter &c) {
    return {c.scale[0], c.scale[1], c.scale[2]};
}

void WriteEnemyScale(CCharacter &c, const EnemyScale &scale) noexcept {
    c.SetScale(scale[0], scale[1], scale[2]);
}

bool OwnedEnemyScale::Claim(std::string_view caller, EnemyScale &current, float multiplier, std::string &error) {
    if (caller.empty()) {
        error = "scale writes require a runtime package owner";
        return false;
    }
    if (!owner.empty() && owner != caller) {
        error = "enemy visual scale is already owned by " + owner;
        return false;
    }
    if (!std::isfinite(multiplier) || multiplier < 0.5f || multiplier > 3.0f) {
        error = "scale multiplier must be finite and between 0.5 and 3.0";
        return false;
    }
    auto       baseline = original;
    EnemyScale requested{};
    for (unsigned i = 0; i < current.size(); ++i) {
        if (!std::isfinite(current[i]) || current[i] <= 0) {
            error = "enemy has invalid native visual scale";
            return false;
        }
        // Rebase only axes changed by native code; repeated requests do not
        // multiply our last applied value again.
        if (owner.empty() || current[i] != applied[i]) {
            baseline[i] = current[i];
        }
        requested[i] = baseline[i] * multiplier;
        if (!std::isfinite(requested[i]) || requested[i] <= 0) {
            error = "requested visual scale overflows native storage";
            return false;
        }
    }
    owner = caller;
    original = baseline;
    applied = requested;
    current = requested;
    error.clear();
    return true;
}

bool OwnedEnemyScale::Release(std::string_view caller, EnemyScale &current) {
    if (owner.empty() || owner != caller) {
        return false;
    }
    bool changed = false;
    for (unsigned i = 0; i < current.size(); ++i) {
        if (current[i] == applied[i] && current[i] != original[i]) {
            current[i] = original[i];
            changed = true;
        }
    }
    *this = {};
    return changed;
}

EnemyScaleOwners &OwnedEnemyScales() {
    static EnemyScaleOwners owners;
    return owners;
}

namespace {
bool Empty(const EnemyScaleOwners::mapped_type &slots) {
    return std::ranges::all_of(slots, [](const auto &record) { return record.owner.empty(); });
}

void Release(CMonstorUnit &unit, unsigned slot, OwnedEnemyScale &record) {
    if (record.owner.empty()) {
        return;
    }
    auto       values = ReadEnemyScale(unit.chara[slot][0]);
    const auto owner = record.owner;
    if (record.Release(owner, values)) {
        WriteEnemyScale(unit.chara[slot][0], values);
    }
}
} // namespace

void PruneOwnedEnemyScales(EnemyScaleOwners &owners) {
    std::erase_if(owners, [](const auto &entry) { return Empty(entry.second); });
}

void ClearOwnedEnemyScales(CMonstorUnit *unit, int slot) {
    auto      &owners = OwnedEnemyScales();
    const auto it = owners.find(unit);
    if (it == owners.end()) {
        return;
    }
    for (unsigned i = 0; i < it->second.size(); ++i) {
        if (slot < 0 || i == static_cast<unsigned>(slot)) {
            Release(*unit, i, it->second[i]);
        }
    }
    if (Empty(it->second)) {
        owners.erase(it);
    }
}

void RetainOwnedEnemyScales(std::span<const std::string> active) {
    auto &owners = OwnedEnemyScales();
    for (auto &[unit, slots] : owners) {
        for (unsigned i = 0; i < slots.size(); ++i) {
            if (!slots[i].owner.empty() && std::ranges::find(active, slots[i].owner) == active.end()) {
                Release(*unit, i, slots[i]);
            }
        }
    }
    PruneOwnedEnemyScales(owners);
}
} // namespace mods
