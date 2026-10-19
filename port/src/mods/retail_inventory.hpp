#pragma once
#include <optional>

#include "runtime.hpp"
class CUserStatus;
class CSaveData;

namespace mods::retail {
// Adapter for existing Chronicle definitions only, not a custom-content registry.
bool           Handles(std::string_view operation);
std::string    Validate(std::string_view operation, const nlohmann::json &arguments);
nlohmann::json Read(const CUserStatus &status, std::string_view operation, const nlohmann::json &arguments, std::optional<int> mardan_fish = {});
void           BuildUp(CSaveData &staged, const nlohmann::json &arguments);
void           Apply(CUserStatus &staged, std::string_view operation, const nlohmann::json &arguments);
} // namespace mods::retail
