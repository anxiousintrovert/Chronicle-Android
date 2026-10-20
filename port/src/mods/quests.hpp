#pragma once
#include <nlohmann/json.hpp>
#include "manager.hpp"

namespace mods {
std::string ValidateQuestOperation(std::string_view operation,const nlohmann::json &arguments);
// Validates saved definitions and combines them with immutable manifest objectives.
std::vector<Quest> QuestDefinitions(const Package &package,const nlohmann::json &state);
nlohmann::json CallOwnedQuest(const Package &package,nlohmann::json &state,std::string_view operation,const nlohmann::json &arguments);
}
