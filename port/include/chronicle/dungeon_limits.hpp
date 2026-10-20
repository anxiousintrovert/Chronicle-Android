#pragma once

// Storage only. Native callers still choose the retail room count and build
// area; expanded generation requires a separate explicit layout request.
namespace chronicle::dungeon {
inline constexpr int RoomCapacity = 64;
inline constexpr int GridStride = 48;
inline constexpr int GridCells = GridStride * GridStride;
inline constexpr int RetailGridExtent = 20;
inline constexpr int RuntimeEventCapacity = 384;
inline constexpr int RetailRuntimeEventCapacity = 96;
inline constexpr int MapEventCapacity = 384;
inline constexpr int RetailMapEventCapacity = 48;
}
