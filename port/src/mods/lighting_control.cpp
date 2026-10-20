#include "lighting_control.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace mods {
namespace {
using Json = nlohmann::json;
void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
bool Number(const Json &value, double low, double high) {
    return value.is_number() && std::isfinite(value.get<double>()) && value.get<double>() >= low && value.get<double>() <= high;
}
void Vector(const Json &value, bool direction, bool bytes) {
    Require(value.is_array() && value.size() == 3, "lighting vectors require three components");
    for (const auto &component : value) {
        Require(Number(component, direction ? -1e6 : 0, direction ? 1e6 : 255), "lighting vector is outside its finite bounds");
        if (bytes) Require(component.is_number_integer(), "fog/background colors require integer RGB bytes");
    }
    if (direction) {
        double length = 0;
        for (const auto &component : value) length += component.get<double>() * component.get<double>();
        Require(length > 1e-12, "light direction must be nonzero");
    }
}
LightingState Decode(const Json &json, int active) {
    LightingState result;
    result.active = active; result.view_scale = json.at("view_scale");
    for (int i = 0; i < 2; ++i) {
        const auto &side = json.at(i == 0 ? "front" : "back");
        auto &out = result.sides[i];
        const auto &fog = side.at("fog");
        out.fog = {fog.at("near"), fog.at("far"), fog.at("min"), fog.at("max")};
        out.fog_color = fog.at("color").get<decltype(out.fog_color)>();
        out.background = side.at("background").get<decltype(out.background)>();
        out.ambient = side.at("ambient").get<decltype(out.ambient)>();
        for (int light = 0; light < 3; ++light) {
            out.direction[light] = side.at("lights")[light].at("direction").get<std::array<float, 3>>();
            out.color[light] = side.at("lights")[light].at("color").get<std::array<float, 3>>();
        }
    }
    return result;
}
}
std::string LightingControl::Validate(std::string_view operation, const Json &args) {
    try {
        Require(args.is_object(), "lighting arguments must be an object");
        if (operation == "dungeon.lighting_reset") {
            Require(args.empty(), "lighting_reset takes no arguments"); return {};
        }
        Require(operation == "dungeon.lighting_set" && !args.empty(), "provide lighting fields");
        bool writes = false;
        for (auto it = args.begin(); it != args.end(); ++it) {
            if (it.key() == "side") {
                Require(it->is_string() && (*it == "front" || *it == "back" || *it == "both"), "side must be front, back or both");
            } else if (it.key() == "view_scale") {
                Require(Number(*it, 0.1, 10), "view_scale must be finite and in 0.1..10"); writes = true;
            } else if (it.key() == "background" || it.key() == "ambient") {
                Vector(*it, false, it.key() == "background"); writes = true;
            } else if (it.key() == "fog") {
                Require(it->is_object() && !it->empty(), "fog must contain fields");
                for (auto field = it->begin(); field != it->end(); ++field) {
                    if (field.key() == "color") Vector(*field, false, true);
                    else {
                        const bool range = field.key() == "near" || field.key() == "far";
                        Require(range || field.key() == "min" || field.key() == "max", "unknown fog field");
                        Require(Number(*field, 0, range ? 100000 : 255), "fog field is outside its finite bounds");
                    }
                }
                writes = true;
            } else if (it.key() == "lights") {
                Require(it->is_array() && !it->empty() && it->size() <= 3, "provide one to three lights");
                std::set<int> ids;
                for (const auto &light : *it) {
                    Require(light.is_object() && light.contains("light") && light["light"].is_number_integer() &&
                            Number(light["light"], 0, 2) && light.size() > 1, "light requires index 0..2 and direction or color");
                    Require(ids.insert(light["light"].get<int>()).second, "duplicate light index");
                    for (auto field = light.begin(); field != light.end(); ++field) {
                        if (field.key() == "light") continue;
                        Require(field.key() == "direction" || field.key() == "color", "unknown light field");
                        Vector(*field, field.key() == "direction", false);
                    }
                }
                writes = true;
            } else throw std::runtime_error("unknown lighting field");
        }
        Require(writes, "provide at least one lighting change");
        return {};
    } catch (const std::exception &e) { return e.what(); }
}
bool LightingControl::Apply(LightingState &state, std::span<const GameCommand> commands, std::string &error) {
    if (commands.empty()) { error.clear(); return true; }
    try {
        auto document = ReadLightingSnapshot(state);
        auto next = claims_;
        std::set<std::string> touched;
        for (const auto &command : commands) {
            const auto validation = Validate(command.operation, command.arguments);
            if (!validation.empty()) throw std::runtime_error(validation);
            Require(!command.owner.empty(), "lighting writes require a package owner");
            if (command.operation == "dungeon.lighting_reset") {
                for (auto it = next.begin(); it != next.end();) {
                    if (it->second.owner != command.owner) { ++it; continue; }
                    auto &value = document.at(Json::json_pointer(it->first));
                    if (value == it->second.last) value = it->second.baseline;
                    it = next.erase(it);
                }
                continue;
            }
            const auto write = [&](const std::string &path, Json value) {
                auto &current = document.at(Json::json_pointer(path));
                auto &claim = next[path];
                if (!claim.owner.empty() && claim.owner != command.owner)
                    throw std::runtime_error("lighting " + path + " is owned by " + claim.owner);
                if (claim.owner.empty()) { claim.owner = command.owner; claim.baseline = current; }
                claim.last = value;
                current = std::move(value);
                touched.insert(path);
            };
            const auto &args = command.arguments;
            if (args.contains("view_scale")) write("/view_scale", args["view_scale"].get<float>());
            const auto side = args.value("side", "both");
            for (const std::string which : {"front", "back"}) {
                if (side != "both" && side != which) continue;
                const std::string prefix = "/" + which + "/";
                for (const char *group : {"fog", "background", "ambient"}) {
                    if (!args.contains(group)) continue;
                    Json value = args[group];
                    if (std::string_view(group) == "fog") {
                        value = document.at(which).at(group);
                        value.update(args[group]);
                        Require(value["near"].get<double>() < value["far"].get<double>(), "fog near must be less than far");
                        Require(value["min"].get<double>() <= value["max"].get<double>(), "fog min must not exceed max");
                    }
                    write(prefix + group, value);
                }
                if (args.contains("lights")) for (const auto &light : args["lights"]) {
                    const auto base = prefix + "lights/" + std::to_string(light["light"].get<int>()) + "/";
                    if (light.contains("color")) write(base + "color", light["color"]);
                    if (light.contains("direction")) {
                        auto vector = light["direction"].get<std::array<double, 3>>();
                        const double length = std::hypot(vector[0], vector[1], vector[2]);
                        write(base + "direction", std::array<float, 3>{float(vector[0]/length), float(vector[1]/length), float(vector[2]/length)});
                    }
                }
            }
        }
        auto staged = Decode(document, state.active);
        for (int i = 0; i < 2; ++i) {
            if (!touched.contains(i == 0 ? "/front/fog" : "/back/fog")) continue;
            const auto &fog = staged.sides[i].fog;
            Require(fog[0] < fog[1] && fog[2] <= fog[3], "fog range collapses at native float precision");
        }
        // Record the float values actually stored, not their input double precision.
        const auto actual = ReadLightingSnapshot(staged);
        for (const auto &path : touched) {
            if (auto it = next.find(path); it != next.end()) it->second.last = actual.at(Json::json_pointer(path));
        }
        claims_.swap(next);
        state = staged;
        error.clear(); return true;
    } catch (const std::exception &e) { error = e.what(); return false; }
}
void LightingControl::Retain(LightingState &state, std::span<const std::string> owners) {
    if (!Active()) return;
    auto document = ReadLightingSnapshot(state);
    auto next = claims_;
    for (auto it = next.begin(); it != next.end();) {
        if (std::ranges::find(owners, it->second.owner) != owners.end()) { ++it; continue; }
        auto &value = document.at(Json::json_pointer(it->first));
        if (value == it->second.last) value = it->second.baseline;
        it = next.erase(it);
    }
    auto staged = Decode(document, state.active);
    claims_.swap(next);
    state = staged;
}
nlohmann::json LightingControl::Owners() const {
    auto result = Json::object();
    for (const auto &[path, claim] : claims_) result[path] = claim.owner;
    return result;
}
}
