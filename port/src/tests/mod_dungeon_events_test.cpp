#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <vector>

#include "chronicle/dungeon_events.hpp"
#include "dun/gameloop.hpp"
#include "dungeoneventman.hpp"
#include "dungeonmap.hpp"
#include "dungeonparts.hpp"
#include "frame.hpp"
#include "framevu1.hpp"

namespace {
std::unique_ptr<CDungeonMap> EventMap(int count, bool expanded) {
    auto map = std::make_unique<CDungeonMap>();
    map->port_grid_extent = expanded ? 48 : 20;
    for (auto &cell : map->cells) {
        cell.parts_no = MAP_PARTS_NONE;
    }
    for (int i = 0; i < count; ++i) {
        map->cells[20 + i % 20 + (20 + i / 20) * 48].parts_no = 0;
    }
    return map;
}

void Definition(CDungeonEventMan &manager, CFrame &frame) {
    auto &definition = manager.slot[0];
    definition.enabled = definition.switch_on = definition.hold = 1;
    definition.parts_id = 0;
    definition.script_no = 200;
    definition.chara_no = -1;
    definition.radius = 1;
    definition.placement_frame = definition.trigger_frame = &frame;
}
} // namespace

TEST(ModDungeonEvents, RetailLimitDoesNotVisitExpandedCellsOrHiddenStorage) {
    auto   manager = std::make_unique<CDungeonEventMan>();
    auto   map = EventMap(100, false);
    CFrame frame;
    Definition(*manager, frame);
    EXPECT_EQ(manager->EventLimit(), 96);
    EXPECT_EQ(manager->GetDataNum(), 96);
    manager->SetupEvent(map.get(), 1);
    EXPECT_EQ(manager->GetDataNum(), 96);
    manager->event[383].Set(&manager->slot[0]);
    manager->SearchDataSwitch(200, 0);
    EXPECT_TRUE(manager->event[383].switch_on);
    EXPECT_EQ(manager->GetDataNum(), 96);
}

TEST(ModDungeonEvents, ExpandedPlacementUsesLastSlotAndNativeSwitchAndPositionQueries) {
    auto   manager = std::make_unique<CDungeonEventMan>();
    auto   map = EventMap(384, true);
    CFrame frame;
    Definition(*manager, frame);
    ASSERT_NO_THROW(manager->SetupEvent(map.get(), 1));
    EXPECT_EQ(manager->EventLimit(), 384);
    EXPECT_EQ(manager->GetDataNum(), 0);
    EXPECT_EQ(manager->SearchDataSlot(), nullptr);
    auto &last = manager->event[383];
    ASSERT_EQ(last.event, &manager->slot[0]);
    EXPECT_FLOAT_EQ(last.pos[0], 23 * 160.0f);
    EXPECT_FLOAT_EQ(last.pos[2], 39 * 160.0f);
    EXPECT_EQ(manager->SearchDataSlotPos(last.pos), &last);
    EXPECT_EQ(manager->SearchDataSlotPos2(last.pos), &last);
    manager->SearchItemEventHold(200);
    EXPECT_EQ(last.hold, 0);
    manager->SearchDataSwitch(200, 0);
    EXPECT_FALSE(last.switch_on);
    EXPECT_EQ(manager->SearchDataSlotPos(last.pos), nullptr);
    manager->SearchDataSwitch(200, 1);
    EXPECT_TRUE(last.switch_on);
}

TEST(ModDungeonEvents, ExhaustionAndMissingFramesRejectBeforePublishingAnySlots) {
    auto   manager = std::make_unique<CDungeonEventMan>();
    auto   map = EventMap(385, true);
    CFrame frame;
    Definition(*manager, frame);
    std::vector<unsigned char> before(sizeof(*manager));
    std::memcpy(before.data(), manager.get(), before.size());
    EXPECT_THROW(manager->SetupEvent(map.get(), 1), std::runtime_error);
    EXPECT_EQ(std::memcmp(manager.get(), before.data(), before.size()), 0);
    map = EventMap(1, true);
    manager->slot[0].trigger_frame = nullptr;
    std::memcpy(before.data(), manager.get(), before.size());
    EXPECT_THROW(manager->SetupEvent(map.get(), 1), std::runtime_error);
    EXPECT_EQ(std::memcmp(manager.get(), before.data(), before.size()), 0);
    EXPECT_EQ(manager->EventLimit(), 96);
}

TEST(ModDungeonEvents, PlacedEventsUseExplicitMapCapacityAndNativeActivation) {
    auto map = std::make_unique<CDungeonMap>();
    EXPECT_EQ(map->MapEventLimit(), 48);
    map->port_grid_extent = 48;
    EXPECT_EQ(map->MapEventLimit(), 384);
    for (auto &event : map->events) {
        event.kind = DNG_EVENT_ATRA;
    }
    map->events[383].kind = DNG_EVENT_NONE;
    float pos[4]{6880, 0, 6880, 1};
    ASSERT_EQ(map->SetTreasureBox(pos, 148, TREASURE_BOX_SMALL, 0), 383);
    EXPECT_EQ(map->events[383].kind, DNG_EVENT_TREASURE_BOX);
    EXPECT_EQ(map->events[383].index, 0);
    EXPECT_EQ(map->boxes[0].used, 1);
    CFrameVu1 player;
    std::memcpy(player.position, map->events[383].pos, sizeof(player.position));
    EXPECT_EQ(map->GetActiveIvent(&player), 383);
    map->port_grid_extent = 20;
    EXPECT_EQ(map->GetActiveIvent(&player), -1);
    map->events[383].kind = DNG_EVENT_MIMIC;
    map->RsetMimicEvent();
    EXPECT_EQ(map->events[383].kind, DNG_EVENT_MIMIC);
    map->port_grid_extent = 48;
    map->RsetMimicEvent();
    EXPECT_EQ(map->events[383].kind, DNG_EVENT_NONE);
    EXPECT_EQ(map->boxes[0].used, 0);
    map->initalize();
    EXPECT_EQ(map->MapEventLimit(), 48);
    for (const auto &event : map->events) {
        EXPECT_EQ(event.kind, DNG_EVENT_NONE);
    }
}

TEST(ModDungeonEvents, PlacedAllocationExhaustionDoesNotChangeMap) {
    auto map = std::make_unique<CDungeonMap>();
    map->port_grid_extent = 48;
    for (auto &event : map->events) {
        event.kind = DNG_EVENT_ATRA;
    }
    std::vector<unsigned char> before(sizeof(*map));
    std::memcpy(before.data(), static_cast<const void *>(map.get()), before.size());
    float pos[4]{6880, 0, 6880, 1};
    EXPECT_THROW(map->SetTreasureBox(pos, 148, TREASURE_BOX_SMALL, 0), std::runtime_error);
    EXPECT_THROW(map->SetAtraBoll(pos, 0), std::runtime_error);
    EXPECT_THROW(map->SetMimicEvent(pos[0], pos[1], pos[2], 148, TREASURE_BOX_SMALL), std::runtime_error);
    EXPECT_EQ(std::memcmp(static_cast<const void *>(map.get()), before.data(), before.size()), 0);
    map->events[383].kind = DNG_EVENT_NONE;
    map->atra_num = 8;
    std::memcpy(before.data(), static_cast<const void *>(map.get()), before.size());
    EXPECT_THROW(map->SetAtraBoll(pos, 0), std::runtime_error);
    EXPECT_EQ(std::memcmp(static_cast<const void *>(map.get()), before.data(), before.size()), 0);
}

TEST(ModDungeonEvents, ExpandedPlacementFindsFarGeometryAndRejectsEmptyMapWithoutRngOrOutputChanges) {
    auto map = std::make_unique<CDungeonMap>();
    map->port_grid_extent = 48;
    for (auto &cell : map->cells) {
        cell.parts_no = MAP_PARTS_NONE;
    }
    selectMapNo = 0;
    float position[4]{1, 2, 3, 4};
    srand(19);
    const int next = rand();
    srand(19);
    EXPECT_THROW(SearchMapPutArea(*map, position), std::runtime_error);
    EXPECT_EQ(rand(), next);
    EXPECT_FLOAT_EQ(position[0], 1);
    EXPECT_FLOAT_EQ(position[3], 4);
    const auto &definition = ItemFreeAreaAll[0][0];
    ASSERT_GT(definition.rect_num, 0);
    map->cells[46 + 46 * 48].parts_no = definition.parts_no;
    for (int seed = 0; seed < 64; ++seed) {
        srand(seed);
        EXPECT_GT(SearchMapPutArea(*map, position), 0);
        EXPECT_GT(position[0], 7000);
        EXPECT_GT(position[2], 7000);
        EXPECT_FLOAT_EQ(position[3], 1);
    }
    for (auto &cell : map->cells) {
        cell.parts_no = definition.parts_no;
    }
    bool beyond_prefix = false;
    for (int seed = 0; seed < 32; ++seed) {
        srand(seed);
        EXPECT_GT(SearchMapPutArea(*map, position), 196);
        beyond_prefix |= position[2] > 20 * 160.0f;
    }
    EXPECT_TRUE(beyond_prefix);
    for (auto &cell : map->cells) {
        cell.parts_no = MAP_PARTS_NONE;
    }
    map->cells[46 + 46 * 48].parts_no = MAP_PARTS_NONE;
    map->cells[3 + 3 * 48].parts_no = definition.parts_no;
    map->port_grid_extent = 20;
    float expected[4];
    srand(24);
    int       count = SearchiDoPutArea(map->cells, 0, 0, 20, 20, expected);
    const int after = rand();
    srand(24);
    EXPECT_EQ(SearchMapPutArea(*map, position), count);
    EXPECT_EQ(std::memcmp(expected, position, sizeof(expected)), 0);
    EXPECT_EQ(rand(), after);
}
