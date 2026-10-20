#pragma once
#include "combat_damage.hpp"
#include "runtime.hpp"
namespace mods {
bool IsCombatRuleWrite(std::string_view operation);
std::string ValidateCombatRule(std::string_view operation, const nlohmann::json &arguments);
nlohmann::json ReadCombatRules(const CombatDamage &state, std::uint64_t epoch);
// A callback may atomically change several owned rules, but cannot mix native
// save/actor writes or currency changes into this registry-only transaction.
bool ApplyCombatRules(CombatDamage &state, const std::vector<GameCommand> &commands,
                      int money, std::uint64_t epoch, std::string &error);
}
