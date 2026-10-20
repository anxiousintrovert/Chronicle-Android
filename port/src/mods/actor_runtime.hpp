#pragma once
#include "runtime.hpp"
namespace mods {
std::string ValidateActorOperation(std::string_view operation, const nlohmann::json &arguments);
nlohmann::json ReadActorOperation(std::string_view operation, const nlohmann::json &arguments);
bool ApplyActorCommands(const std::vector<GameCommand> &commands, int money, std::string &error);
void RetainGameActors();
}
