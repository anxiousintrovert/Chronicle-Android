#pragma once

namespace chronicle::monsters {
inline constexpr unsigned RetailSlots = 16;
inline constexpr unsigned StorageCapacity = 256;
// Framework storage follows native storage; iteration uses each unit's active
// limit, which remains retail unless explicitly changed at a loading boundary.
inline constexpr unsigned SlotCapacity = StorageCapacity;
static_assert(SlotCapacity <= StorageCapacity);
}
