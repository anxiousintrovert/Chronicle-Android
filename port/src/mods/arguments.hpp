#pragma once
#include "runtime.hpp"
struct lua_State;

namespace mods {
// Shared transport limits, independent of any engine/content domain.
std::string ValidateArguments(const nlohmann::json &value);
bool        ReadArguments(lua_State *lua, int index, nlohmann::json &value, std::string &error);
} // namespace mods
