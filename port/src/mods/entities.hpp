#pragma once
#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
class CMonstorUnit;

namespace mods {
std::string    ValidateEntityQuery(std::string_view operation, const nlohmann::json &arguments);
nlohmann::json ReadEntities(CMonstorUnit *unit, std::string_view operation, const nlohmann::json &arguments);
} // namespace mods
