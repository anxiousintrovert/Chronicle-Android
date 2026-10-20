#pragma once

#include "runtime.hpp"

class CSaveData;

namespace mods {
std::string    ValidatePlayerCombat(std::string_view operation, const nlohmann::json &arguments);
nlohmann::json ReadPlayerCombat();

class PlayerCombatTransaction {
public:
    bool Stage(CSaveData &save, std::span<const GameCommand> commands, std::string &error);
    void Commit() noexcept;

private:
    int frames_ = 0;
};
} // namespace mods
