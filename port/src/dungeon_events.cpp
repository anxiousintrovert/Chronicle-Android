#include "chronicle/dungeon_events.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "dun/gameloop.hpp"
#include "dungeoneventman.hpp"
#include "dungeonmap.hpp"
#include "dungeonparts.hpp"

void RequireExpandedMapEventSlot(const CDungeonMap &map) {
    if (map.GridExtent() != chronicle::dungeon::GridStride) {
        return;
    }
    for (const auto &event : map.events) {
        if (event.kind == DNG_EVENT_NONE) {
            return;
        }
    }
    throw std::runtime_error("expanded dungeon placed map-event capacity exceeded");
}

void PrepareExpandedDungeonEvents(CDungeonEventMan &manager, const CDungeonMap &map) {
    int available = 0, required = 0;
    for (const auto &event : manager.event) {
        if (!event.event || !event.enabled) {
            ++available;
        }
    }
    for (int z = 0; z < map.GridExtent(); ++z) {
        for (int x = 0; x < map.GridExtent(); ++x) {
            const int part = map.cells[x + z * chronicle::dungeon::GridStride].parts_no;
            if (part == MAP_PARTS_NONE) {
                continue;
            }
            if (part < 0 || part >= 72) {
                throw std::runtime_error("invalid expanded event map part");
            }
            for (const auto &definition : manager.slot) {
                if (!definition.enabled || definition.parts_id != part) {
                    continue;
                }
                if (!definition.placement_frame || !definition.trigger_frame) {
                    throw std::runtime_error("expanded event definition has no placement/trigger frame");
                }
                if (++required > available) {
                    throw std::runtime_error("expanded dungeon runtime event capacity exceeded");
                }
            }
        }
    }
    manager.port_event_limit = chronicle::dungeon::RuntimeEventCapacity;
}

int SearchMapPutArea(CDungeonMap &map, float *position) {
    using namespace chronicle::dungeon;
    if (map.GridExtent() != GridStride) {
        return SearchiDoPutArea(map.cells, 0, 0, RetailGridExtent, RetailGridExtent, position);
    }
    if (selectMapNo < 0 || selectMapNo >= 7 || !ItemFreeAreaAll[selectMapNo]) {
        throw std::runtime_error("expanded placement has no dungeon geometry");
    }

    // Keep all native rectangles, including those beyond the retail prefix.
    struct Candidate {
        int                   cell;
        const ITEM_FREE_AREA *area;
        int                   rectangle;
    };

    std::vector<Candidate> candidates;
    auto                  *areas = ItemFreeAreaAll[selectMapNo];
    for (int cell = 0; cell < GridCells; ++cell) {
        if (map.cells[cell].parts_no == MAP_PARTS_NONE) {
            continue;
        }
        for (int a = 0; areas[a].parts_no != -1; ++a) {
            if (areas[a].parts_no != map.cells[cell].parts_no) {
                continue;
            }
            if (areas[a].rect_num < 0 || areas[a].rect_num > 4 ||
                areas[a].direction < 0 || areas[a].direction > 3 ||
                map.cells[cell].direction < 0 || map.cells[cell].direction > 3) {
                throw std::runtime_error("invalid expanded placement geometry");
            }
            for (int r = 0; r < areas[a].rect_num; ++r) {
                const auto &rect = areas[a].rect[r];
                for (float value : {rect.x0, rect.y0, rect.z0, rect.x1, rect.y1, rect.z1}) {
                    if (!std::isfinite(value) || std::abs(value) > 100000.0f) {
                        throw std::runtime_error("invalid expanded placement rectangle");
                    }
                }
                candidates.push_back({cell, &areas[a], r});
            }
        }
    }
    if (candidates.empty()) {
        throw std::runtime_error("expanded floor has no item placement area");
    }
    const auto  pick = std::min(static_cast<std::size_t>((static_cast<double>(rand()) / 2147483648.0) * candidates.size()), candidates.size() - 1);
    const auto &candidate = candidates[pick];
    const auto &rect = candidate.area->rect[candidate.rectangle];
    const int   turn = (candidate.area->direction + map.cells[candidate.cell].direction) % 4;
    const float angle = (3.14159265358979323846f * (90.0f * (4 - turn))) / 180.0f;
    float       min_x = INFINITY, max_x = -INFINITY, min_z = INFINITY, max_z = -INFINITY;
    for (float x : {rect.x0 * 10.0f, rect.x1 * 10.0f}) {
        for (float z : {rect.z0 * 10.0f, rect.z1 * 10.0f}) {
            const float world_x = z * sinf(angle) + x * cosf(angle) + 160.0f * (candidate.cell % GridStride);
            const float world_z = -x * sinf(angle) + z * cosf(angle) + 160.0f * (candidate.cell / GridStride);
            min_x = std::min(min_x, world_x);
            max_x = std::max(max_x, world_x);
            min_z = std::min(min_z, world_z);
            max_z = std::max(max_z, world_z);
        }
    }
    position[0] = min_x + (max_x - min_x) * (static_cast<double>(rand()) / 2147483648.0);
    position[1] = rect.y0 * 10.0f;
    position[2] = min_z + (max_z - min_z) * (static_cast<double>(rand()) / 2147483648.0);
    position[3] = 1.0f;
    return static_cast<int>(candidates.size());
}
