#pragma once
#include <memory>
#include <span>

#include "runtime.hpp"

namespace mods {
bool           HasOwnedLayouts();
bool           IsLayoutWrite(std::string_view operation);
std::string    ValidateLayout(std::string_view operation, const nlohmann::json &arguments);
nlohmann::json ReadLayout(const nlohmann::json &arguments);
void           RetainOwnedLayouts(std::span<const std::string> owners);
bool           BuildOwnedDungeonLayout();

class LayoutTransaction {
public:
    LayoutTransaction();
    ~LayoutTransaction();
    bool Stage(const std::vector<GameCommand> &commands, std::string &error);
    void Commit() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> staged_;
};
} // namespace mods
