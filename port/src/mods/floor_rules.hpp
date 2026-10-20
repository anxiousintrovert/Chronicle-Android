#pragma once
#include "runtime.hpp"
class CDngStatusData;

namespace mods {
std::string    ValidateFloorRules(std::string_view operation, const nlohmann::json &args);
nlohmann::json ReadFloorRules(const CDngStatusData &status, const nlohmann::json &args);
void           ApplyFloorRules(CDngStatusData &status, const nlohmann::json &args, bool active_context);
} // namespace mods
