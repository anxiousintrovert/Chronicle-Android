#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <tuple>

namespace mods {
struct DamageContext {
    int weapon = -1, model = -1, character = -1, hp = -1, max_hp = -1;
    std::uint64_t frame = 0;
};
// Detached, copyable registry. Stage a copy before publishing a callback's
// writes. Hit evaluation allocates nothing and never touches native RNG/state.
class DamageModifiers {
public:
    static constexpr unsigned Capacity = 128, OwnerCapacity = 32;
    static std::string Validate(const nlohmann::json &arguments);
    bool Set(std::string_view owner, const nlohmann::json &arguments, std::string &error);
    bool Remove(std::string_view owner, std::string_view id);
    void Retain(std::span<const std::string> owners);
    void Clear() { rules_.clear(); }
    bool Empty() const { return rules_.empty(); }
    std::size_t Size() const { return rules_.size(); }
    float Apply(float damage, const DamageContext &context) noexcept;
    nlohmann::json State() const;
private:
    enum class Operation { Add, Multiply, Set, Chance, Random, Ramp };
    struct Definition {
        Operation operation = Operation::Multiply;
        double value = 1, chance = 0, minimum = 0, maximum = 0, rate = 0;
        int priority = 0;
        std::uint32_t seed = 0;
        std::uint64_t reset = 1;
        std::optional<int> weapon, model, character;
        std::optional<double> below;
        bool operator==(const Definition &) const = default;
    };
    struct Rule {
        Definition definition;
        std::uint64_t random_key = 0, draws = 0, start = 0, last = 0;
        bool started = false;
    };
    using Key = std::tuple<int, std::string, std::string>;
    std::map<Key, Rule> rules_;
    static Definition Decode(const nlohmann::json &arguments);
};
}
