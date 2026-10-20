#pragma once
#include <span>

#include "runtime.hpp"

class CUserStatus;
class CSaveData;

namespace mods {
struct GeoramaState;

struct Operation {
    std::string_view name, permission, description;
    bool             writes;
};

std::span<const Operation> Operations();
const Operation           *FindOperation(std::string_view name);
std::string                ValidateOperation(std::string_view name, const nlohmann::json &arguments);

// Bounded retail notifications to dispatch only after a successful commit.
// This is not a rollback contract for arbitrary resource or scene side effects.
// Captured by the game bridge only while the native model/cache are ready.
struct EquipmentContext {
    int character = -1, item = -1, element = -1;
};

struct CommitEffects {
    unsigned status_sounds = 0;
    bool     refresh_weapon = false;
};

// Detached values only. No polling or snapshot allocation until a mod calls.
nlohmann::json ReadStatus(const CUserStatus &status, const World &world,
                          std::string_view operation, const nlohmann::json &arguments);
// Validate and stage the entire callback before changing any native state.
bool           ApplyStatus(CUserStatus &status, const std::vector<GameCommand> &commands,
                           int money_delta, std::string &error, CommitEffects *effects = nullptr);
nlohmann::json ReadSave(CSaveData &save, const World &world, std::string_view operation, const nlohmann::json &arguments, const GeoramaState *georama = nullptr);
bool           ApplySave(CSaveData &save, const std::vector<GameCommand> &commands, int money_delta, std::string &error, CommitEffects *effects = nullptr, GeoramaState *georama = nullptr, const EquipmentContext *equipment = nullptr, bool town_loadout = false, bool active_floor = false);
} // namespace mods
