#include "damage_modifiers.hpp"
#include <algorithm>
#include <cmath>
#include <string_view>

namespace mods {
namespace {
using Json = nlohmann::json;
bool Identifier(std::string_view value, unsigned maximum) {
    if (value.empty() || value.size() > maximum) return false;
    for (unsigned char c : value) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
    return true;
}
bool Number(const Json &value, double minimum, double maximum, bool integer = false) {
    return (integer ? value.is_number_integer() : value.is_number()) && std::isfinite(value.get<double>()) &&
        value.get<double>() >= minimum && value.get<double>() <= maximum;
}
std::uint64_t Hash(std::string_view owner, std::string_view id, std::uint32_t seed) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : owner) { hash ^= c; hash *= 1099511628211ULL; }
    hash ^= 0; hash *= 1099511628211ULL;
    for (unsigned char c : id) { hash ^= c; hash *= 1099511628211ULL; }
    for (unsigned shift = 0; shift < 32; shift += 8) { hash ^= (seed >> shift) & 255; hash *= 1099511628211ULL; }
    return hash;
}
double Draw(std::uint64_t key, std::uint64_t ordinal) noexcept {
    std::uint64_t word = key + 0x9e3779b97f4a7c15ULL * (ordinal + 1);
    word = (word ^ (word >> 30)) * 0xbf58476d1ce4e5b9ULL;
    word = (word ^ (word >> 27)) * 0x94d049bb133111ebULL;
    word ^= word >> 31;
    return static_cast<double>(word >> 11) * 0x1.0p-53;
}
}
std::string DamageModifiers::Validate(const Json &a) {
    if (!a.is_object()) return "damage modifier must be an object";
    for (const auto &[key, value] : a.items())
        if (key != "id" && key != "operation" && key != "value" && key != "priority" && key != "seed" &&
            key != "chance" && key != "min_value" && key != "max_value" && key != "rate_per_second" &&
            key != "reset_after_frames" && key != "weapon_id" && key != "native_model_id" &&
            key != "character_id" && key != "target_hp_below_percent") return "unsupported damage modifier field: " + key;
    if (!a.contains("id") || !a["id"].is_string() || !Identifier(a["id"].get_ref<const std::string &>(), 64) ||
        !a.contains("operation") || !a["operation"].is_string()) return "provide a local modifier id and operation";
    const auto &op = a["operation"].get_ref<const std::string &>();
    if (op != "add" && op != "multiply" && op != "set" && op != "chance_multiply" && op != "random_multiply" && op != "ramp_multiply")
        return "unsupported damage modifier operation";
    if (op == "random_multiply") {
        if (a.contains("value")) return "random_multiply takes bounds instead of value";
    } else if (!a.contains("value") || !Number(a["value"], op == "add" ? -1000000 : op == "ramp_multiply" ? 1 : 0,
        op == "add" || op == "set" ? 1000000 : 100)) return "damage modifier value is outside its allowed range";
    if (a.contains("priority") && !Number(a["priority"], -1000, 1000, true)) return "priority must be an integer within +/-1000";
    if (a.contains("seed") && (!Number(a["seed"], 0, 4294967295.0, true) || (op != "chance_multiply" && op != "random_multiply")))
        return "seed is a uint32 for chance/random modifiers only";
    if (op == "chance_multiply") {
        if (!a.contains("chance") || !Number(a["chance"], 0, 100)) return "chance must be 0..100";
    } else if (a.contains("chance")) return "chance is only valid for chance_multiply";
    if (op == "random_multiply") {
        if (!a.contains("min_value") || !a.contains("max_value") || !Number(a["min_value"], 0, 100) ||
            !Number(a["max_value"], 0, 100) || a["min_value"].get<double>() > a["max_value"].get<double>()) return "random bounds must satisfy 0 <= min_value <= max_value <= 100";
    } else if (a.contains("min_value") || a.contains("max_value")) return "random bounds are only valid for random_multiply";
    if (op == "ramp_multiply") {
        if (!a.contains("rate_per_second") || !Number(a["rate_per_second"], 0, 10) ||
            !a.contains("reset_after_frames") || !Number(a["reset_after_frames"], 1, 36000, true)) return "ramp requires rate_per_second 0..10 and reset_after_frames 1..36000";
    } else if (a.contains("rate_per_second") || a.contains("reset_after_frames")) return "ramp fields require ramp_multiply";
    for (const auto *field : {"weapon_id", "native_model_id", "character_id"})
        if (a.contains(field) && !Number(a[field], 0, 1000000, true)) return std::string(field) + " must be an integer 0..1000000";
    if (a.contains("target_hp_below_percent") && !Number(a["target_hp_below_percent"], 0, 100)) return "HP threshold must be finite and within 0..100";
    return {};
}
DamageModifiers::Definition DamageModifiers::Decode(const Json &a) {
    Definition d;
    const auto &op = a.at("operation").get_ref<const std::string &>();
    d.operation = op == "add" ? Operation::Add : op == "set" ? Operation::Set : op == "chance_multiply" ? Operation::Chance :
                  op == "random_multiply" ? Operation::Random : op == "ramp_multiply" ? Operation::Ramp : Operation::Multiply;
    d.value = a.value("value", 1.0); d.priority = a.value("priority", 0); d.seed = a.value("seed", std::uint32_t(0));
    d.chance = a.value("chance", 0.0); d.minimum = a.value("min_value", 0.0); d.maximum = a.value("max_value", 0.0);
    d.rate = a.value("rate_per_second", 0.0); d.reset = a.value("reset_after_frames", std::uint64_t(1));
    if (a.contains("weapon_id")) d.weapon = a["weapon_id"].get<int>();
    if (a.contains("native_model_id")) d.model = a["native_model_id"].get<int>();
    if (a.contains("character_id")) d.character = a["character_id"].get<int>();
    if (a.contains("target_hp_below_percent")) d.below = a["target_hp_below_percent"].get<double>();
    return d;
}
bool DamageModifiers::Set(std::string_view owner, const Json &a, std::string &error) {
    if (!Identifier(owner, 96)) { error = "damage modifier requires a bounded owner id"; return false; }
    error = Validate(a); if (!error.empty()) return false;
    const auto definition = Decode(a); const auto &id = a.at("id").get_ref<const std::string &>();
    auto existing = rules_.end(); unsigned count = 0;
    for (auto it = rules_.begin(); it != rules_.end(); ++it) if (std::get<1>(it->first) == owner) {
        ++count; if (std::get<2>(it->first) == id) existing = it;
    }
    if (existing != rules_.end() && existing->second.definition == definition) return true;
    if (existing == rules_.end() && (rules_.size() >= Capacity || count >= OwnerCapacity)) { error = "damage modifier capacity reached"; return false; }
    // Allocate a replacement node before removing the old definition. Updating
    // priority must update the ordering key, not only a field in the record.
    decltype(rules_) prepared;
    prepared.emplace(Key{definition.priority, std::string(owner), id}, Rule{definition, Hash(owner, id, definition.seed)});
    auto node = prepared.extract(prepared.begin());
    if (existing != rules_.end()) rules_.erase(existing);
    rules_.insert(std::move(node)); return true;
}
bool DamageModifiers::Remove(std::string_view owner, std::string_view id) {
    for (auto it = rules_.begin(); it != rules_.end(); ++it)
        if (std::get<1>(it->first) == owner && std::get<2>(it->first) == id) { rules_.erase(it); return true; }
    return false;
}
void DamageModifiers::Retain(std::span<const std::string> owners) {
    std::erase_if(rules_, [&](const auto &entry) { return std::ranges::find(owners, std::get<1>(entry.first)) == owners.end(); });
}
float DamageModifiers::Apply(float damage, const DamageContext &c) noexcept {
    // Preserve even signed zero/nonfinite inputs when no positive hit can be
    // modified. An empty/nonmatching registry does not clamp retail damage.
    if (rules_.empty() || !std::isfinite(damage) || damage <= 0) return damage;
    double result = damage;
    for (auto &[key, rule] : rules_) {
        const auto &d = rule.definition;
        if ((d.weapon && *d.weapon != c.weapon) || (d.model && *d.model != c.model) ||
            (d.character && *d.character != c.character) ||
            (d.below && (c.hp < 0 || c.max_hp <= 0 || 100.0 * c.hp >= *d.below * c.max_hp))) continue;
        switch (d.operation) {
            case Operation::Chance: if (100 * Draw(rule.random_key, rule.draws++) >= d.chance) continue;
                                   result *= d.value; break;
            case Operation::Random: result *= d.minimum + (d.maximum - d.minimum) * Draw(rule.random_key, rule.draws++); break;
            case Operation::Ramp:
                if (!rule.started || c.frame < rule.last || c.frame - rule.last > d.reset) { rule.start = c.frame; rule.started = true; }
                rule.last = c.frame;
                result *= std::min(d.value, 1.0 + d.rate * (static_cast<double>(c.frame - rule.start) / 60.0)); break;
            case Operation::Add: result += d.value; break;
            case Operation::Multiply: result *= d.value; break;
            case Operation::Set: result = d.value; break;
        }
        if (result <= 0) return 0;
        result = std::min(result, 1000000.0);
    }
    return static_cast<float>(result);
}
nlohmann::json DamageModifiers::State() const {
    Json state = Json::array();
    for (const auto &[key, rule] : rules_) {
        const auto &d = rule.definition;
        static constexpr const char *names[]{"add", "multiply", "set", "chance_multiply", "random_multiply", "ramp_multiply"};
        Json entry = {{"owner", std::get<1>(key)}, {"id", std::get<2>(key)}, {"priority", d.priority},
            {"operation", names[static_cast<unsigned>(d.operation)]}, {"value", d.value}};
        if (d.operation == Operation::Random) entry.erase("value");
        if (d.weapon) entry["weapon_id"] = *d.weapon;
        if (d.model) entry["native_model_id"] = *d.model;
        if (d.character) entry["character_id"] = *d.character;
        if (d.below) entry["target_hp_below_percent"] = *d.below;
        if (d.operation == Operation::Chance) entry["chance"] = d.chance;
        if (d.operation == Operation::Random) { entry["min_value"] = d.minimum; entry["max_value"] = d.maximum; }
        if (d.operation == Operation::Random || d.operation == Operation::Chance) { entry["seed"] = d.seed; entry["draws"] = rule.draws; }
        if (d.operation == Operation::Ramp) { entry["rate_per_second"] = d.rate; entry["reset_after_frames"] = d.reset; }
        state.push_back(std::move(entry));
    }
    return state;
}
}
