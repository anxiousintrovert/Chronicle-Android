#pragma once
#include <memory>
#include <span>

#include "runtime.hpp"

namespace mods {
bool           IsLootWrite(std::string_view operation);
std::string    ValidateLoot(std::string_view operation, const nlohmann::json &arguments);
nlohmann::json ReadLoot(const nlohmann::json &arguments);
nlohmann::json ReadChestLoot(const nlohmann::json &arguments);
bool           HasOwnedLoot();
void           RetainOwnedLoot(std::span<const std::string> owners);

class LootTransaction {
public:
    LootTransaction();
    ~LootTransaction();
    bool Stage(const std::vector<GameCommand> &commands, bool writable, std::string &error);
    void Commit() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> staged_;
};
} // namespace mods
