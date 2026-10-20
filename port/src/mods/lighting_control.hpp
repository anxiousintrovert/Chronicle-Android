#pragma once
#include <map>
#include "lighting.hpp"
namespace mods {
// Detached floor-local transactions. No native pointers or automatic hooks.
class LightingControl {
public:
    static std::string Validate(std::string_view operation, const nlohmann::json &arguments);
    bool Apply(LightingState &state, std::span<const GameCommand> commands, std::string &error);
    void Retain(LightingState &state, std::span<const std::string> owners);
    void Forget() { claims_.clear(); }
    bool Active() const { return !claims_.empty(); }
    nlohmann::json Owners() const;
private:
    struct Claim { std::string owner; nlohmann::json baseline, last; };
    std::map<std::string, Claim> claims_;
};
}
