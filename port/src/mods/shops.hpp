#pragma once
#include <memory>
#include <span>

#include "runtime.hpp"

namespace mods {
bool           IsShopWrite(std::string_view operation);
std::string    ValidateShop(std::string_view operation, const nlohmann::json &arguments);
nlohmann::json ReadShop(std::string_view operation, const nlohmann::json &arguments);
bool           HasOwnedShops();
void           RetainOwnedShops(std::span<const std::string> owners);

class ShopTransaction {
public:
    ShopTransaction();
    ~ShopTransaction();
    bool Stage(const std::vector<GameCommand> &commands, bool writable, std::string &error);
    void Commit() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> staged_;
};
} // namespace mods
