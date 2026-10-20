#pragma once

#include "runtime.hpp"

namespace mods {
std::string    ValidateTownSelection(std::string_view operation, const nlohmann::json &args);
nlohmann::json ReadTownSelection();
bool           QueueTownSelection(const std::vector<GameCommand> &commands, int money, std::string &error);
void           FinishTownSelection();
void           RetireTownScene();
void           TownBodyLoaded();
} // namespace mods
