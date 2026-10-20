#pragma once

#include <cstdint>
#include <cstring>

#include "dungeonmap.hpp"

namespace chronicle::dungeon {
// Moon Sea's retail scenery samples beyond a 20-cell row and occasionally
// beyond the cell array. Preserve that logical layout when storage grows:
// prefix reads still see the event tail, suffix reads see the first 16 room
// records followed by room_num/boxes. Use object-byte reads rather than an
// out-of-bounds MAP_CELL subscript. Expanded layouts need their own bounds.
inline int RetailDummyPart(const CDungeonMap &map, int x, int z) {
    constexpr int cells = RetailGridExtent * RetailGridExtent;
    const int     index = x + z * RetailGridExtent;
    if (index >= 0 && index < cells) {
        return map.cells[index % RetailGridExtent + (index / RetailGridExtent) * GridStride].parts_no;
    }
    std::uintptr_t address;
    if (index < 0) {
        address = reinterpret_cast<std::uintptr_t>(map.cells) - static_cast<std::uintptr_t>(-index) * sizeof(MAP_CELL);
    } else {
        auto offset = static_cast<std::size_t>(index - cells) * sizeof(MAP_CELL);
        if (offset >= 16 * sizeof(ROOM_INFO)) {
            offset += sizeof(map.rooms) - 16 * sizeof(ROOM_INFO);
        }
        address = reinterpret_cast<std::uintptr_t>(map.rooms) + offset;
    }
    int part;
    std::memcpy(&part, reinterpret_cast<const void *>(address), sizeof(part));
    return part;
}
} // namespace chronicle::dungeon
