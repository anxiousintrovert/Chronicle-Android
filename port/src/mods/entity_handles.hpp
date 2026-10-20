#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include "chronicle/monster_limits.hpp"

namespace mods {
// Session-local identities for the retail monster slots. This registry is dormant
// until requested, never changes native storage, and must be notified at every
// native slot construction and floor clear before handles may be exposed.
class EntityHandles {
public:
    static constexpr unsigned Capacity = chronicle::monsters::SlotCapacity;
    using Handle = std::uint64_t;

    void Activate() { active_ = true; }

    bool Active() const { return active_; }

    void Deactivate() {
        Clear();
        active_ = false;
    }

    void Clear() { slots_.fill(0); }

    // A generation is never reused, including after clear/deactivation. Exhaustion
    // fails closed instead of allowing a stale handle to identify another actor.
    Handle Construct(unsigned slot) {
        if (!active_ || slot >= Capacity) {
            return 0;
        }
        slots_[slot] = 0;
        if (next_ > std::numeric_limits<Handle>::max() / Capacity) {
            return 0;
        }
        const auto result = next_++ * Capacity + slot;
        slots_[slot] = result;
        return result;
    }

    void Retire(unsigned slot) {
        if (slot < Capacity) {
            slots_[slot] = 0;
        }
    }

    Handle Get(unsigned slot) const {
        return active_ && slot < Capacity ? slots_[slot] : 0;
    }

    std::optional<unsigned> Resolve(Handle handle) const {
        if (!active_ || !handle) {
            return std::nullopt;
        }
        const auto slot = static_cast<unsigned>(handle % Capacity);
        if (slots_[slot] != handle) {
            return std::nullopt;
        }
        return slot;
    }

private:
    bool                         active_ = false;
    Handle                       next_ = 1;
    std::array<Handle, Capacity> slots_{};
};
} // namespace mods
