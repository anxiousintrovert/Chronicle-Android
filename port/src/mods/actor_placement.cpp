#include "actor_spawn.hpp"
#include "dungeonmap.hpp"
#include "dungeonparts.hpp"
#include "monstorunit.hpp"
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

namespace mods {
bool ValidateActorObstructions(const CDungeonMap &map, CMonstorUnit &unit,
                               std::array<float, 3> position, float radius,
                               std::array<float, 3> player_position, float player_radius, std::string &error) {
    const auto fail = [&](const char *reason) { error = reason; return false; };
    const auto valid_point = [](const auto &point) {
        for (int i = 0; i < 3; ++i) if (!std::isfinite(point[i]) || std::abs(point[i]) > 1000000.0f) return false;
        return true;
    };
    const auto valid_radius = [](float value) { return std::isfinite(value) && value >= 0 && value <= 1000; };
    if (!valid_point(position) || !valid_point(player_position) || !valid_radius(radius) || !valid_radius(player_radius))
        return fail("invalid actor obstruction request");
    const auto blocked = [&](const auto &point, float clearance) {
        return !valid_point(point) || !valid_radius(clearance) ||
            std::hypot(double(position[0]) - point[0], double(position[1]) - point[1], double(position[2]) - point[2]) <= double(radius) + clearance;
    };
    if (blocked(player_position, player_radius)) return fail("actor spawn overlaps the player");
    for (unsigned slot = 0; slot < unit.SlotLimit(); ++slot) {
        const auto &actor = unit.monster[slot]; if (actor.state == -1) continue;
        float point[4]{}; unit.chara[slot][0].GetPosition(point);
        if (!valid_radius(actor.collision_radius) || blocked(point, std::max(13.0f, actor.collision_radius))) return fail("actor spawn overlaps an occupied monster slot");
    }
    if (map.box_num < 0 || map.box_num > static_cast<int>(std::size(map.boxes)) ||
        map.atra_num < 0 || map.atra_num > static_cast<int>(std::size(map.atra))) return fail("invalid floor object counts");
    for (int i = 0; i < map.box_num; ++i)
        if (map.boxes[i].used && blocked(map.boxes[i].pos, 20)) return fail("actor spawn overlaps a treasure box");
    for (int i = 0; i < map.atra_num; ++i)
        if (map.atra[i].used && blocked(map.atra[i].pos, 20)) return fail("actor spawn overlaps an Atla ball");
    for (const auto &trap : map.trap_circle)
        if (trap.state && blocked(trap.pos, 20)) return fail("actor spawn overlaps a trap circle");
    for (int i = 0; i < map.MapEventLimit(); ++i) {
        const auto &event = map.events[i];
        if (event.kind != DNG_EVENT_NONE && (!valid_radius(event.radius) || blocked(event.pos, std::max(20.0f, event.radius))))
            return fail("actor spawn overlaps a dungeon event");
    }
    error.clear(); return true;
}
}
