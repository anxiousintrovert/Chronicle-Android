#pragma once

namespace chronicle::monsters {
inline constexpr unsigned RetailSlots = 16;
inline constexpr unsigned StorageCapacity = 256;
// Framework storage follows native storage; iteration uses each unit's active
// limit, which remains retail unless explicitly changed at a loading boundary.
inline constexpr unsigned SlotCapacity = StorageCapacity;
static_assert(SlotCapacity <= StorageCapacity);

// Retail permits fewer than three neighbors within 480 units. An explicit
// expanded population above 192 gets a fourth neighbor; minimum spacing and
// terrain/treasure/Atla checks remain native. Small maps can still reject it.
constexpr int PlacementNeighborLimit(int requested, unsigned active_slots) {
    if (active_slots <= RetailSlots || active_slots > StorageCapacity ||
        requested < 1 || requested > int(StorageCapacity)) return 3;
    const int density = (requested + 63) / 64;
    return density > 3 ? density : 3;
}
}
