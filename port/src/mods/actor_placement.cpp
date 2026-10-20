#include "actor_spawn.hpp"
#include "dungeonmap.hpp"
#include "dungeonparts.hpp"
#include "chronicle/dungeon_limits.hpp"
#include <algorithm>
#include <cmath>

extern ITEM_FREE_AREA ItemFreeAreaD01[27], ItemFreeAreaD02[15], ItemFreeAreaD03[27], ItemFreeAreaD04[27];
extern ITEM_FREE_AREA ItemFreeAreaD05[27], ItemFreeAreaD06[15], ItemFreeAreaD07[28];
namespace mods {
std::span<const ITEM_FREE_AREA> LoadedActorPlacementAreas(int dungeon) {
    // Bounds belong to the shipped native arrays, never to an arbitrary pointer
    // ending in a guessed sentinel. Reject a replaced/unknown table pointer.
    if (dungeon < 0 || dungeon >= 7) return {};
    const auto *table = ItemFreeAreaAll[dungeon];
    if (table == ItemFreeAreaD01) return ItemFreeAreaD01;
    if (table == ItemFreeAreaD02) return ItemFreeAreaD02;
    if (table == ItemFreeAreaD03) return ItemFreeAreaD03;
    if (table == ItemFreeAreaD04) return ItemFreeAreaD04;
    if (table == ItemFreeAreaD05) return ItemFreeAreaD05;
    if (table == ItemFreeAreaD06) return ItemFreeAreaD06;
    if (table == ItemFreeAreaD07) return ItemFreeAreaD07;
    return {};
}
bool ValidateActorPlacement(const CDungeonMap &map, std::span<const ITEM_FREE_AREA> areas,
                            std::array<float, 3> position, float radius, std::string &error) {
    const auto fail = [&](const char *reason) { error = reason; return false; };
    for (const auto value : position)
        if (!std::isfinite(value) || std::abs(value) > 1000000.0f) return fail("invalid actor placement position");
    if (!std::isfinite(radius) || radius < 0 || radius > 1000) return fail("invalid actor placement radius");
    const int extent = map.GridExtent();
    if (extent != chronicle::dungeon::RetailGridExtent && extent != chronicle::dungeon::GridStride)
        return fail("unsupported dungeon grid extent");
    std::size_t count = 0;
    while (count < areas.size() && areas[count].parts_no != -1) {
        const auto &area = areas[count++];
        if (area.rect_num < 0 || area.rect_num > 4 || area.direction < 0 || area.direction > 3)
            return fail("invalid actor placement geometry");
        for (int r = 0; r < area.rect_num; ++r)
            for (float v : {area.rect[r].x0, area.rect[r].y0, area.rect[r].z0, area.rect[r].x1, area.rect[r].y1, area.rect[r].z1})
                if (!std::isfinite(v) || std::abs(v) > 100000.0f) return fail("invalid actor placement rectangle");
    }
    if (count == areas.size()) return fail("actor placement table has no bounded terminator");
    bool inside = false;
    for (int z = 0; z < extent; ++z) for (int x = 0; x < extent; ++x) {
        const auto &cell = map.cells[x + z * chronicle::dungeon::GridStride];
        if (cell.parts_no == MAP_PARTS_NONE) continue;
        if (cell.direction < 0 || cell.direction > 3) return fail("invalid actor placement cell direction");
        for (std::size_t a = 0; a < count; ++a) {
            const auto &area = areas[a]; if (area.parts_no != cell.parts_no) continue;
            const int turn = (area.direction + cell.direction) % 4;
            // Exact quarter-turn inverse of native placement, avoiding random
            // selection and trigonometric drift at room edges.
            const float dx = position[0] - x * 160.0f, dz = position[2] - z * 160.0f;
            const float local_x = turn == 0 ? dx : turn == 1 ? dz : turn == 2 ? -dx : -dz;
            const float local_z = turn == 0 ? dz : turn == 1 ? -dx : turn == 2 ? -dz : dx;
            for (int r = 0; r < area.rect_num; ++r) {
                const auto &rect = area.rect[r];
                inside |= std::abs(position[1] - rect.y0 * 10.0f) <= 1.0f &&
                    local_x >= std::min(rect.x0, rect.x1) * 10.0f + radius &&
                    local_x <= std::max(rect.x0, rect.x1) * 10.0f - radius &&
                    local_z >= std::min(rect.z0, rect.z1) * 10.0f + radius &&
                    local_z <= std::max(rect.z0, rect.z1) * 10.0f - radius;
            }
        }
    }
    if (!inside) return fail("actor position is outside a native placement area with radius clearance");
    error.clear(); return true;
}
}
