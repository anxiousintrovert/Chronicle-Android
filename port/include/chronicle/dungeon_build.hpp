#pragma once

#include <stdexcept>

#include "chronicle/dungeon_limits.hpp"

namespace chronicle::dungeon {
// Internal generation context. Host operations must validate ownership and
// publish a complete floor before exposing this to gameplay.
struct BuildLayout {
    int rooms;
    int area;
    int room_min;
    int room_max;
    int chests = 0; // Zero keeps the retail target; otherwise requested generated chests.
    int monsters = 0; // Zero keeps native population; explicit requests reserve one of 16 slots.
};

inline thread_local const BuildLayout *requested_build = nullptr;

class BuildScope {
public:
    explicit BuildScope(BuildLayout layout) : layout_(layout), previous_(requested_build) {
        if (layout.rooms < 2 || layout.rooms > RoomCapacity ||
            layout.area < 8 || layout.area > GridStride - 2 ||
            layout.room_min < 3 || layout.room_max > 6 ||
            layout.room_min > layout.room_max ||
            (layout.chests != 0 && (layout.chests < 2 || layout.chests > 24)) ||
            layout.monsters < 0 || layout.monsters > 15) {
            throw std::invalid_argument("invalid dungeon layout dimensions");
        }
        requested_build = &layout_;
    }

    ~BuildScope() { requested_build = previous_; }

    BuildScope(const BuildScope &) = delete;
    BuildScope &operator=(const BuildScope &) = delete;

private:
    BuildLayout        layout_;
    const BuildLayout *previous_;
};

inline bool CustomBuild() { return requested_build != nullptr; }

inline int BuildExtent() { return CustomBuild() ? GridStride : RetailGridExtent; }

inline int LinkExtent() { return CustomBuild() ? GridStride : 16; }

inline int BuildArea() { return CustomBuild() ? requested_build->area : 16; }

inline int RoomMinimum() { return CustomBuild() ? requested_build->room_min : 3; }

inline int RoomSpan() { return CustomBuild() ? requested_build->room_max - requested_build->room_min + 1 : 2; }

inline int BuildOrigin() { return CustomBuild() ? 1 : 0; }

inline int ChestTarget(int retail_target) {
    return CustomBuild() && requested_build->chests != 0 ? requested_build->chests : retail_target;
}

inline void RequireBuild(bool condition, const char *reason) {
    if (CustomBuild() && !condition) {
        throw std::runtime_error(reason);
    }
}
} // namespace chronicle::dungeon
