#pragma once
#include "runtime.hpp"
namespace mods {
std::string ValidateGeoramaPlacement(std::string_view operation, const nlohmann::json &args);
nlohmann::json ReadGeoramaPlacement();
bool ApplyGeoramaPlacement(const std::vector<GameCommand> &commands, int money, std::string &error);
}
