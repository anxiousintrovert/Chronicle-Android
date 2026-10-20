#pragma once

namespace chronicle::monsters {
inline constexpr unsigned RetailSlots = 16;
inline constexpr unsigned StorageCapacity = 256;
// Framework indexing stays retail until lifecycle, script allocation and
// per-unit active bounds can activate the additional native storage together.
inline constexpr unsigned SlotCapacity = RetailSlots;
static_assert(SlotCapacity <= StorageCapacity);
}
