#pragma once

#include <array>

#include "runtime.hpp"

namespace mods {
// Internal transaction state; native hooks and public catalog registration are separate.
class PlayerTuning {
public:
    enum Field : std::size_t {
        RunSpeed,
        AttackSpeed,
        Recoil,
        GaugeRefill,
        ThirstDrain,
        DurabilityWear,
        InvincibleBonus,
        MapReveal,
        MonsterReveal,
        Count
    };

    static bool        IsWrite(std::string_view operation);
    static std::string Validate(std::string_view operation, const nlohmann::json &arguments);

    // Stage on a detached copy alongside other engine transactions, then Swap to commit.
    // A failed batch leaves this instance unchanged, including its epoch and claims.
    bool Stage(std::span<const GameCommand> commands, std::uint64_t epoch, std::string &error);
    void Retain(std::span<const std::string> owners, std::uint64_t epoch);
    void Swap(PlayerTuning &other) noexcept;

    bool Engaged() const { return engaged_; }

    double Value(Field field) const { return values_[field]; }

    nlohmann::json State() const;

private:
    void                           Clear(std::string_view owner);
    void                           Refresh();
    std::array<double, Count>      values_{1, 1, 1, 1, 1, 1, 0, 0, 0};
    std::array<std::string, Count> owners_;
    std::uint64_t                  epoch_ = 0;
    bool                           engaged_ = false;
};
} // namespace mods
