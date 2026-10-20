#pragma once
#include <array>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include "chronicle/monster_limits.hpp"

class CCharacter;
class CMonstorUnit;

namespace mods {
using EnemyScale = std::array<float, 3>;

struct OwnedEnemyScale {
    std::string owner;
    EnemyScale  original{}, applied{};
    bool        Claim(std::string_view caller, EnemyScale &current, float multiplier, std::string &error);
    bool        Release(std::string_view caller, EnemyScale &current);
};

using EnemyScaleOwners = std::map<CMonstorUnit *, std::array<OwnedEnemyScale, chronicle::monsters::SlotCapacity>>;
EnemyScale        ReadEnemyScale(const CCharacter &character);
void              WriteEnemyScale(CCharacter &character, const EnemyScale &scale) noexcept;
EnemyScaleOwners &OwnedEnemyScales();
void              PruneOwnedEnemyScales(EnemyScaleOwners &owners);
void              ClearOwnedEnemyScales(CMonstorUnit *unit, int slot = -1);
void              RetainOwnedEnemyScales(std::span<const std::string> active_owners);
} // namespace mods
