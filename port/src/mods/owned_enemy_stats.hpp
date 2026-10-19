#pragma once
#include <array>
#include <map>
#include <span>
#include <vector>

#include "owned_stat.hpp"

class CMonstorUnit;
struct MONSTOR;

namespace mods {
// Health fields are coupled when maximum HP changes. Remaining fields can be
// owned independently by different packages. No native layout is extended.
using EnemyStatValues = std::array<int, 6>;
using EnemyStatClaims = std::array<OwnedStat, 6>;
using EnemyStatOwners = std::map<CMonstorUnit *, std::array<EnemyStatClaims, 16>>;
inline constexpr std::array<std::string_view, 6> EnemyStatNames{
    "hp", "max_hp", "defense", "reward_exp", "reward_money", "drop_item"};
EnemyStatValues  ReadEnemyStats(const MONSTOR &actor);
void             WriteEnemyStats(MONSTOR &actor, const EnemyStatValues &values) noexcept;
EnemyStatOwners &OwnedEnemyStats();
void             PruneOwnedEnemyStats(EnemyStatOwners &owners);
int              ScaleEnemyHealth(int hp, int old_max, int new_max);
void             ReleaseEnemyStats(EnemyStatClaims &claims, EnemyStatValues &values, std::string_view owner);
void             ClearOwnedEnemyStats(CMonstorUnit *unit, int slot = -1);
void             RetainOwnedEnemyStats(std::span<const std::string> active_owners);
} // namespace mods
