#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <map>
#include <string_view>

namespace mods {
std::string ValidateSkillOperation(std::string_view operation,const nlohmann::json &arguments);
std::map<std::pair<std::string,std::string>,double> SkillDamageFactors(const nlohmann::json &state,bool incoming=false);
void ValidateSkillsState(const nlohmann::json &state);
nlohmann::json CallOwnedSkill(nlohmann::json &state,std::string_view operation,const nlohmann::json &arguments);
}
