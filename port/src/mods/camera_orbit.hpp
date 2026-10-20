#pragma once
#include <array>
#include "runtime.hpp"
class CCameraFollow;
namespace mods {
// Scene-local ownership state. The live adapter must discard it before camera
// reinitialization; it deliberately stores no pointer to an engine object.
class CameraOrbit {
public:
    static std::string Validate(std::string_view operation, const nlohmann::json &args);
    bool Apply(CCameraFollow &camera, std::span<const GameCommand> commands, std::string &error);
    void Retain(CCameraFollow &camera, std::span<const std::string> owners);
    void Forget() { claims_ = {}; }
    bool Active() const;
    nlohmann::json Owners() const;
private:
    struct Claim {
        std::string owner;
        float baseline = 0, last = 0;
    };
    std::array<Claim, 3> claims_;
};
}
