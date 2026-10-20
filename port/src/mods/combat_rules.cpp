#include "combat_rules.hpp"
namespace mods {
using Json = nlohmann::json;
bool IsCombatRuleWrite(std::string_view op) { return op == "combat.rule_set" || op == "combat.rule_remove"; }
std::string ValidateCombatRule(std::string_view op, const Json &a) {
    if (!a.is_object()) return "combat rule arguments must be an object";
    if (op == "combat.rules_get") return a.empty() ? "" : "combat.rules_get takes no arguments";
    if (!IsCombatRuleWrite(op)) return "unknown combat rule operation";
    if (!a.contains("direction") || !a["direction"].is_string() ||
        (a["direction"] != "outgoing" && a["direction"] != "incoming")) return "direction must be outgoing or incoming";
    if (op == "combat.rule_set") {
        if (a.size() != 2 || !a.contains("rule")) return "provide only direction and rule";
        auto error = DamageModifiers::Validate(a["rule"]); if (!error.empty()) return error;
        if (a["direction"] == "incoming" && a["rule"].contains("native_model_id")) return "incoming deductions have no native attacker model identity";
        if (a["rule"].contains("character_id") && a["rule"]["character_id"].get<int>() > 5) return "character_id must be 0..5";
        if (a["rule"].contains("native_model_id") && a["rule"]["native_model_id"].get<int>() >= 167) return "native_model_id must be 0..166";
        return {};
    }
    if (a.size() != 2 || !a.contains("id")) return "provide only direction and id";
    return DamageModifiers::Validate({{"id",a["id"]},{"operation","multiply"},{"value",1}});
}
Json ReadCombatRules(const CombatDamage &state, std::uint64_t epoch) {
    const bool current = state.epoch == epoch;
    return {{"epoch",epoch},{"frame",current ? state.frame : 0},
            {"outgoing",current ? state.outgoing.State() : Json::array()},
            {"incoming",current ? state.incoming.State() : Json::array()}};
}
bool ApplyCombatRules(CombatDamage &state, const std::vector<GameCommand> &commands, int money, std::uint64_t epoch, std::string &error) {
    error.clear();
    if (money || commands.empty()) { error = "combat rule transactions require rule commands and no currency changes"; return false; }
    for (const auto &c : commands) {
        if (!IsCombatRuleWrite(c.operation)) { error = "combat rule changes cannot share a callback with other engine writes"; return false; }
        error = ValidateCombatRule(c.operation,c.arguments); if (!error.empty()) return false;
        // Validate the runtime-assigned owner even for idempotent removal.
        DamageModifiers owner_check;
        if (!owner_check.Set(c.owner, {{"id","check"},{"operation","multiply"},{"value",1}},error)) return false;
    }
    CombatDamage next = state.epoch == epoch ? state : CombatDamage{}; next.epoch = epoch;
    for (const auto &c : commands) {
        auto &rules = c.arguments["direction"] == "outgoing" ? next.outgoing : next.incoming;
        if (c.operation == "combat.rule_set") {
            if (!rules.Set(c.owner,c.arguments["rule"],error)) return false;
        } else rules.Remove(c.owner,c.arguments["id"].get_ref<const std::string &>());
    }
    state = std::move(next); return true;
}
}
