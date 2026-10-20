#pragma once
#include <nlohmann/json.hpp>
#include <string_view>

namespace mods {
// Pure data transforms: no scripts, engine calls, files, or live state writes.
void ValidateSaveMigrations(const nlohmann::json &rules, std::string_view current_version);
bool MigrateSavedState(nlohmann::json &state, const nlohmann::json &rules, std::string_view from_version);
}
