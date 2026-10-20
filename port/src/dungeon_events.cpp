#include "chronicle/dungeon_events.hpp"

#include <stdexcept>

#include "dungeoneventman.hpp"
#include "dungeonmap.hpp"

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
