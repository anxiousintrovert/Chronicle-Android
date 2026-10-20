#pragma once
#include "chronicle/monster_limits.hpp"

namespace chronicle::monsters {
inline constexpr unsigned RetailHitRecords = 96;
// Sixteen attack spheres retained for two ticks, plus the shared retail pool.
inline constexpr unsigned HitRecordsPerSlot = 32;
inline constexpr unsigned HitStorageCapacity = RetailHitRecords + StorageCapacity * HitRecordsPerSlot;
}
