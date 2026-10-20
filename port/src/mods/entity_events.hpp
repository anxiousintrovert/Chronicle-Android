#pragma once
#include <array>
#include <cstdint>
#include <span>
class CMonstorUnit;

namespace mods {
struct EnemyDeath {
    std::uint64_t entity = 0;
    int           dungeon = -1, floor = -1, attacker = -1, experience = 0, money = 0, damage = 0;
    bool          scripted = false;
    CMonstorUnit *unit = nullptr;
};

// Native producers store only bounded value records. Mod callbacks are never
// invoked here. The runtime must drain and validate context before dispatch.
class EnemyDeathQueue {
public:
    void Enable(bool enabled) {
        enabled_ = enabled;
        if (!enabled) {
            Reset();
        }
    }

    bool Enabled() const { return enabled_; }

    void Reset() {
        size_ = 0;
        dropped_ = 0;
    }

    void Push(const EnemyDeath &event) {
        if (!enabled_) {
            return;
        }
        if (size_ == records_.size()) {
            if (dropped_ != UINT32_MAX) {
                ++dropped_;
            }
            return;
        }
        records_[size_++] = event;
    }

    std::span<const EnemyDeath> Pending() const { return {records_.data(), size_}; }

    std::uint32_t Dropped() const { return dropped_; }

    void CancelEntity(CMonstorUnit *unit, std::uint64_t entity) {
        unsigned kept = 0;
        for (unsigned i = 0; i < size_; ++i)
            if (records_[i].unit != unit || records_[i].entity != entity) records_[kept++] = records_[i];
        size_ = kept;
    }

    void CancelUnit(CMonstorUnit *unit) {
        unsigned kept = 0;
        for (unsigned i = 0; i < size_; ++i) {
            if (records_[i].unit != unit) {
                records_[kept++] = records_[i];
            }
        }
        size_ = kept;
    }

private:
    bool                       enabled_ = false;
    unsigned                   size_ = 0;
    std::uint32_t              dropped_ = 0;
    std::array<EnemyDeath, 64> records_{};
};

EnemyDeathQueue &EnemyDeaths();
void             CaptureEnemyDeath(CMonstorUnit *unit, int slot, bool scripted);
} // namespace mods
