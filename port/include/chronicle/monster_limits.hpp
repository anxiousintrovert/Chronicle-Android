#pragma once

namespace chronicle::monsters {
inline constexpr unsigned RetailSlots = 16;
// Increase only with the native arrays, lifecycle, script allocation and
// per-unit active bounds. Additional storage must not activate retail slots.
inline constexpr unsigned SlotCapacity = RetailSlots;
}
