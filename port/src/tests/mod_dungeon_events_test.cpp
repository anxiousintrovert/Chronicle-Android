#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <vector>

#include "dungeoneventman.hpp"
#include "dungeonmap.hpp"
#include "frame.hpp"

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
