#include "camera_orbit.hpp"
#include <algorithm>
#include <cmath>
#include "camerafollow.hpp"

namespace mods {
namespace {
constexpr std::array<const char *, 3> names{"distance", "height", "angle"};
constexpr std::array<float, 3> minimum{8, -80, -1000}, maximum{300, 150, 1000};
std::array<float, 3> Read(CCameraFollow &camera) {
    return {camera.GetDistance(), camera.GetHeight(), camera.next_angle};
}
void Write(CCameraFollow &camera, const std::array<float, 3> &before, const std::array<float, 3> &after) {
    if (before[0] != after[0] && std::isfinite(after[0])) camera.SetDistance(after[0]);
    if (before[1] != after[1] && std::isfinite(after[1])) camera.SetHeight(after[1]);
    if (before[2] != after[2] && std::isfinite(after[2])) camera.SetAngle(after[2]);
}
}
std::string CameraOrbit::Validate(std::string_view operation, const nlohmann::json &args) {
    if (!args.is_object()) return "camera arguments must be an object";
    if (operation == "render.camera_reset") return args.empty() ? "" : "camera_reset takes no arguments";
    if (operation != "render.camera_set" || args.empty()) return "provide at least one camera orbit field";
    for (auto it = args.begin(); it != args.end(); ++it) {
        const auto where = std::ranges::find(names, it.key());
        if (where == names.end()) return "unknown camera orbit field: " + it.key();
        const auto index = where - names.begin();
        if (!it->is_number() || !std::isfinite(it->get<double>()) ||
            it->get<double>() < minimum[index] || it->get<double>() > maximum[index])
            return "camera orbit field is outside its finite bounds: " + it.key();
    }
    return {};
}
bool CameraOrbit::Apply(CCameraFollow &camera, std::span<const GameCommand> commands, std::string &error) {
    if (commands.empty()) { error.clear(); return true; }
    if (!camera.follow_on) { error = "camera is not following"; return false; }
    const auto before = Read(camera);
    if (std::ranges::any_of(before, [](float value) { return !std::isfinite(value); })) {
        error = "native camera orbit is not finite"; return false;
    }
    auto values = before;
    auto next = claims_;
    for (const auto &command : commands) {
        error = Validate(command.operation, command.arguments);
        if (!error.empty()) return false;
        if (command.owner.empty()) { error = "camera writes require a package owner"; return false; }
        if (command.operation == "render.camera_reset") {
            for (std::size_t i = 0; i < next.size(); ++i) {
                auto &claim = next[i];
                if (claim.owner != command.owner) continue;
                if (values[i] == claim.last) values[i] = claim.baseline;
                claim = {};
            }
            continue;
        }
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (!command.arguments.contains(names[i])) continue;
            auto &claim = next[i];
            if (!claim.owner.empty() && claim.owner != command.owner) {
                error = std::string("camera ") + names[i] + " is owned by " + claim.owner;
                return false;
            }
            if (claim.owner.empty()) { claim.owner = command.owner; claim.baseline = values[i]; }
            float value = command.arguments[names[i]].get<float>();
            if (i == 2) value = std::remainder(value, 6.2831853071795864769f);
            claim.last = values[i] = value;
        }
    }
    claims_.swap(next);
    Write(camera, before, values);
    error.clear();
    return true;
}
void CameraOrbit::Retain(CCameraFollow &camera, std::span<const std::string> owners) {
    if (!Active()) return;
    const auto before = Read(camera);
    auto after = before;
    for (std::size_t i = 0; i < claims_.size(); ++i) {
        auto &claim = claims_[i];
        if (claim.owner.empty() || std::ranges::find(owners, claim.owner) != owners.end()) continue;
        if (before[i] == claim.last) after[i] = claim.baseline;
        claim = {};
    }
    Write(camera, before, after);
}
bool CameraOrbit::Active() const {
    return std::ranges::any_of(claims_, [](const auto &claim) { return !claim.owner.empty(); });
}
nlohmann::json CameraOrbit::Owners() const {
    return {{"distance", claims_[0].owner}, {"height", claims_[1].owner}, {"angle", claims_[2].owner}};
}
}
