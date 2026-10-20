#pragma once
#include "runtime.hpp"
struct lua_State;

namespace mods {
struct ArgumentLimits { unsigned nodes=128, bytes=16384; };
ArgumentLimits OperationArgumentLimits(std::string_view operation);
// Defaults remain small; bounded content declarations may explicitly opt in.
std::string ValidateArguments(const nlohmann::json &value, ArgumentLimits limits = {});
bool        ReadArguments(lua_State *lua, int index, nlohmann::json &value, std::string &error, ArgumentLimits limits = {});
} // namespace mods
