#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace mods {
std::string ValidateSkillOperation(std::string_view operation,const nlohmann::json &arguments);
void ValidateSkillsState(const nlohmann::json &state);
nlohmann::json CallOwnedSkill(nlohmann::json &state,std::string_view operation,const nlohmann::json &arguments);
}
