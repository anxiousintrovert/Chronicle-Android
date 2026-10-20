#pragma once

// Storage only. Native callers still choose the retail room count and build
// area; expanded generation requires a separate explicit layout request.
namespace chronicle::dungeon {
inline constexpr int RoomCapacity = 64;
}
