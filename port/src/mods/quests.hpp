#pragma once
#include <nlohmann/json.hpp>
#include <functional>
#include "manager.hpp"

namespace mods {
std::string ValidateQuestOperation(std::string_view operation,const nlohmann::json &arguments);
// Validates saved definitions and combines them with immutable manifest objectives.
nlohmann::json QuestStatusJson(const Quest &quest,const nlohmann::json &state);
bool WantsQuestEvent(const nlohmann::json &state,std::string_view event);
std::vector<nlohmann::json> QuestEventAdvances(const Package &package,const nlohmann::json &state,std::string_view event,const nlohmann::json &payload);
std::vector<Quest> QuestDefinitions(const Package &package,const nlohmann::json &state);
nlohmann::json CallOwnedQuest(const Package &package,nlohmann::json &state,std::string_view operation,const nlohmann::json &arguments, const std::function<void(const nlohmann::json &)> &enqueue_rewards = {});
}
