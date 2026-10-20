#include "dungeon_minimap.hpp"

#include <cmath>

#include "dungeonmap.hpp"

std::vector<DungeonMiniMapSprite> ExpandedDungeonMiniMap(
    const CDungeonMap &map, const float *position, int status, bool reveal_map, bool reveal_items) {
    std::vector<DungeonMiniMapSprite> result;
    if (map.GridExtent() != chronicle::dungeon::GridStride || status < 0 || status >= 3) {
        return result;
    }
    constexpr int           left = 380, top = 64, tile = 5;
    constexpr float         scale = tile / 160.0f;
    constexpr unsigned char hidden[] = {0x20, 0x40, 0x60};
    constexpr unsigned char seen[] = {0x60, 0x80, 0xa2};
    constexpr unsigned char opacity[] = {0x40, 0x60, 0xa2};
    result.reserve(chronicle::dungeon::GridCells + 36);
    for (int z = 0; z < map.GridExtent(); ++z) {
        for (int x = 0; x < map.GridExtent(); ++x) {
            const int   index = x + z * chronicle::dungeon::GridStride;
            const auto &cell = map.cells[index];
            if (cell.parts_no < 0 || cell.parts_no >= 72 || (!map.mask[index] && !reveal_map)) {
                continue;
            }
            const int direction = cell.direction + map.parts[cell.parts_no].direction_offset;
            if (direction < 0) {
                continue;
            }
            result.push_back({DungeonMiniMapKind::Tile,
                              CRect_i_(left + x * tile, top + z * tile, tile, tile),
                              CRect_i_((direction % 8) * 16, (direction / 8) * 16, 16, 16),
                              map.mask[index] ? seen[status] : hidden[status], opacity[status]});
        }
    }
    auto cell_at = [&](const float *p) {
        if (!std::isfinite(p[0]) || !std::isfinite(p[2])) {
            return -1;
        }
        const float x = (p[0] + 80.0f) / 160.0f, z = (p[2] + 80.0f) / 160.0f;
        if (x < 0 || z < 0 || x >= map.GridExtent() || z >= map.GridExtent()) {
            return -1;
        }
        return int(x) + int(z) * chronicle::dungeon::GridStride;
    };
    auto center_x = [&](const float *p) { return int(left + p[0] * scale + tile * .5f); };
    auto center_y = [&](const float *p) { return int(top + p[2] * scale + tile * .5f); };
    if (cell_at(position) >= 0) {
        result.push_back({DungeonMiniMapKind::Player,
                          CRect_i_(center_x(position), center_y(position), 12, 12),
                          CRect_i_(88, 96, 12, 12), 0x80, 0x80});
    }
    auto marker = [&](DungeonMiniMapKind kind, const float *p, int u, int v) {
        const int index = cell_at(p);
        if (index < 0 || map.cells[index].parts_no < 0 || map.cells[index].parts_no >= 72 ||
            (!map.mask[index] && !reveal_items)) {
            return;
        }
        result.push_back({kind, CRect_i_(center_x(p) - 4, center_y(p) - 4, 8, 8),
                          CRect_i_(u, v, 8, 8), 0x80, opacity[status]});
    };
    for (const auto &trap : map.trap_circle) {
        if (trap.state) {
            marker(DungeonMiniMapKind::Trap, trap.pos, 88, 112);
        }
    }
    for (const auto &box : map.boxes) {
        if (box.used && box.closed) {
            marker(DungeonMiniMapKind::Chest, box.pos, 80, 96);
        }
    }
    for (const auto &atla : map.atra) {
        if (atla.used) {
            marker(DungeonMiniMapKind::Atla, atla.pos, 72, 104);
        }
    }
    return result;
}
