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
};

inline thread_local const BuildLayout *requested_build = nullptr;

class BuildScope {
public:
    explicit BuildScope(BuildLayout layout) : layout_(layout), previous_(requested_build) {
        if (layout.rooms < 2 || layout.rooms > RoomCapacity ||
            layout.area < 8 || layout.area > GridStride - 2 ||
            layout.room_min < 3 || layout.room_max > 6 ||
            layout.room_min > layout.room_max) {
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

inline void RequireBuild(bool condition, const char *reason) {
    if (CustomBuild() && !condition) {
        throw std::runtime_error(reason);
    }
}
} // namespace chronicle::dungeon
