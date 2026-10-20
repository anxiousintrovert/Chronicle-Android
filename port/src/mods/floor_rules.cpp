#include "floor_rules.hpp"

#include <algorithm>
#include <stdexcept>

#include "dngstatusdata.hpp"
#include "monstorunit.hpp"

namespace mods {
namespace {
bool Integer(const nlohmann::json &a, const char *key, int low, int high) {
    return a.contains(key) && a[key].is_number_integer() && a[key].get<double>() >= low && a[key].get<double>() <= high;
}

bool DefinedRule(int rule) { return (rule >= -1 && rule <= 5) || rule == 10 || rule == 11; }
} // namespace

std::string ValidateFloorRules(std::string_view operation, const nlohmann::json &a) {
    if (!a.is_object()) {
        return "arguments must be an object";
    }
    if (!Integer(a, "dungeon", 0, 5)) {
        return "dungeon must identify a native restriction table (0-5)";
    }
    const int dungeon = a["dungeon"];
    if (!Integer(a, "floor", 0, std::min(25, maxFloorTbl__3[dungeon]) - 1)) {
        return "floor must be a valid zero-based retail floor";
    }
    const bool write = operation == "dungeon.floor_rules_set";
    for (const auto &[key, value] : a.items()) {
        if (key != "dungeon" && key != "floor" && !(write && (key == "rule" || key == "apply_current"))) {
            return "unknown floor rule argument";
        }
    }
    if (write) {
        if (!Integer(a, "rule", -1, 11) || !DefinedRule(a["rule"].get<int>())) {
            return "rule must be -1, 0-5, 10 or 11";
        }
        if (a.contains("apply_current") && !a["apply_current"].is_boolean()) {
            return "apply_current must be boolean";
        }
    }
    return {};
}

nlohmann::json ReadFloorRules(const CDngStatusData &s, const nlohmann::json &a) {
    const auto error = ValidateFloorRules("dungeon.floor_rules_get", a);
    if (!error.empty()) {
        throw std::runtime_error(error);
    }
    const int dungeon = a["dungeon"], floor = a["floor"];
    return {
        {"dungeon",     dungeon                                 },
        {"floor",       floor                                   },
        {"rule",        int(s.res_limit_zone_id[dungeon][floor])},
        {"active_rule", s.res_limit_zone_current                }
    };
}

void ApplyFloorRules(CDngStatusData &s, const nlohmann::json &a, bool active_context) {
    const auto error = ValidateFloorRules("dungeon.floor_rules_set", a);
    if (!error.empty()) {
        throw std::runtime_error(error);
    }
    const int  dungeon = a["dungeon"], floor = a["floor"], rule = a["rule"];
    const bool active = a.value("apply_current", false);
    if (active && (!active_context || s.cur_georama != dungeon || s.cur_floor != floor)) {
        throw std::runtime_error("active application requires the requested live dungeon floor");
    }
    if (active && rule >= 0 && rule <= 5 && (s.cur_chara != rule || rule >= s.party_size)) {
        throw std::runtime_error("switch to the required joined character before applying this restriction");
    }
    s.res_limit_zone_id[dungeon][floor] = static_cast<char>(rule);
    if (active) {
        s.res_limit_zone_current = rule;
    }
}
} // namespace mods
