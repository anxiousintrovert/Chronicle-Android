#pragma once
#include <span>

#include "runtime.hpp"

class CUserStatus;
class CSaveData;

namespace mods {
struct Operation {
    std::string_view name, permission, description;
    bool             writes;
};

std::span<const Operation> Operations();
const Operation           *FindOperation(std::string_view name);
std::string                ValidateOperation(std::string_view name, const nlohmann::json &arguments);
// Detached values only. No polling or snapshot allocation until a mod calls.
nlohmann::json ReadStatus(const CUserStatus &status, const World &world,
                          std::string_view operation, const nlohmann::json &arguments);
// Validate and stage the entire callback before changing any native state.
bool           ApplyStatus(CUserStatus &status, const std::vector<GameCommand> &commands,
                           int money_delta, std::string &error);
nlohmann::json ReadSave(CSaveData &save, const World &world, std::string_view operation, const nlohmann::json &arguments);
bool           ApplySave(CSaveData &save, const std::vector<GameCommand> &commands, int money_delta, std::string &error);
} // namespace mods
