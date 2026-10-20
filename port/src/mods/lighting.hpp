#pragma once
#include <array>
#include "runtime.hpp"
namespace mods {
struct LightingSide {
    std::array<float, 4> fog{};
    std::array<unsigned char, 3> fog_color{}, background{};
    std::array<float, 3> ambient{};
    std::array<std::array<float, 3>, 3> direction{}, color{};
};
struct LightingState {
    std::array<LightingSide, 2> sides;
    int active = 0;
    float view_scale = 0;
};
nlohmann::json ReadLightingSnapshot(const LightingState &state);
nlohmann::json ReadDungeonLighting();
bool ApplyDungeonLighting(const std::vector<GameCommand> &commands, int money, std::string &error);
}
