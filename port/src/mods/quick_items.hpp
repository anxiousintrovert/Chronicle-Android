#pragma once
#include "runtime.hpp"
class CSaveData;
class CUserStatus;

namespace mods {
std::string    ValidateQuickItems(std::string_view operation, const nlohmann::json &arguments);
nlohmann::json ReadQuickItems();
std::string    ValidateQuickSelection(CSaveData &save, const CUserStatus &staged, int slot);
void           CommitQuickSelection(int slot) noexcept;
} // namespace mods
