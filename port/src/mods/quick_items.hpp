#pragma once
#include "runtime.hpp"
class CSaveData;
class CUserStatus;

namespace mods {
std::string    ValidateQuickItems(std::string_view operation, const nlohmann::json &arguments);
nlohmann::json ReadQuickItems();
std::string    ValidateQuickSelection(CSaveData &save, const CUserStatus &staged, int slot);
void           CommitQuickSelection(int slot) noexcept;

struct QuickUseRequest {
    std::string   owner;
    std::uint64_t epoch = 0;
    int           slot = 0, item = -1, character = -1, dungeon = -1, floor = -1;
    std::int64_t  deadline = 0;
    CSaveData    *save = nullptr;
    bool          pressed = false;
};

QuickUseRequest PrepareQuickUse(CSaveData &save, const CUserStatus &staged, int slot, std::string owner);
void            CommitQuickUse(QuickUseRequest request) noexcept;
void            CancelQuickUse() noexcept;
void            RetainQuickUse();

struct QuickUseInput {
    bool held = false, down = false;
};

QuickUseInput StepQuickUse();
void          AcknowledgeQuickUsePress() noexcept;
} // namespace mods
