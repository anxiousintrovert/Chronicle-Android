#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <memory>

#include "chronicle/dungeon_grid.hpp"
#include "dungeonmap.hpp"

TEST(ModDungeonCapacity, ExpandedRoomVisibilityStorageClearsWithoutChangingCellsOrRng) {
    static_assert(chronicle::dungeon::RoomCapacity == 64);
    auto map = std::make_unique<CDungeonMap>();
    ASSERT_EQ(std::size(map->rooms), 64u);
    ASSERT_EQ(std::size(map->room_seen), 64u);
    ASSERT_EQ(std::size(map->cells), 2304u);
    for (auto &cell : map->cells) {
        cell.parts_no = MAP_PARTS_NONE;
    }
    std::fill(std::begin(map->room_seen), std::end(map->room_seen), 1);
    std::fill(std::begin(map->mask), std::end(map->mask), 1);
    srand(19);
    const int expected = rand();
    srand(19);
    map->FlushCheckMask();
    EXPECT_TRUE(std::all_of(std::begin(map->room_seen), std::end(map->room_seen), [](int value) { return value == 0; }));
    EXPECT_TRUE(std::all_of(std::begin(map->mask), std::end(map->mask), [](int value) { return value == 0; }));
    EXPECT_TRUE(std::all_of(std::begin(map->cells), std::end(map->cells), [](const auto &cell) { return cell.parts_no == MAP_PARTS_NONE; }));
    EXPECT_EQ(rand(), expected);
}

TEST(ModDungeonCapacity, VisibilityCanAddressLastRoomWithoutChangingOtherRoomFlags) {
    auto map = std::make_unique<CDungeonMap>();
    std::fill(std::begin(map->room_seen), std::end(map->room_seen), 0);
    std::fill(std::begin(map->mask), std::end(map->mask), 0);
    for (auto &room : map->rooms) {
        room = {};
    }
    map->rooms[63] = {2, 2, 3, 3};
    map->room_num = 64;
    map->checkMask(320.0f, 320.0f);
    EXPECT_EQ(map->room_seen[63], 1);
    EXPECT_EQ(std::count(std::begin(map->room_seen), std::end(map->room_seen), 1), 1);
    EXPECT_EQ(std::count(std::begin(map->mask), std::end(map->mask), 1), 9);
    EXPECT_EQ(map->mask[4 + 4 * chronicle::dungeon::GridStride], 1);
    std::fill(std::begin(map->room_seen), std::end(map->room_seen), 0);
    std::fill(std::begin(map->mask), std::end(map->mask), 0);
    map->rooms[63] = {43, 43, 3, 3};
    map->checkMask(6880.0f, 6880.0f);
    EXPECT_EQ(map->room_seen[63], 1);
    EXPECT_EQ(std::count(std::begin(map->mask), std::end(map->mask), 1), 9);
    EXPECT_EQ(map->mask[45 + 45 * chronicle::dungeon::GridStride], 1);
}

TEST(ModDungeonCapacity, RetailSceneryPreservesRowWrappingAndAdjacentFieldReads) {
    auto map = std::make_unique<CDungeonMap>();
    map->cells[chronicle::dungeon::GridStride].parts_no = 123;
    map->cells[19].parts_no = 456;
    map->cells[20].parts_no = 999;
    EXPECT_EQ(chronicle::dungeon::RetailDummyPart(*map, 20, 0), 123);
    EXPECT_EQ(chronicle::dungeon::RetailDummyPart(*map, -1, 1), 456);
    map->rooms[0].x = 777;
    map->room_num = 42;
    EXPECT_EQ(chronicle::dungeon::RetailDummyPart(*map, 0, 20), 777);
    EXPECT_EQ(chronicle::dungeon::RetailDummyPart(*map, 16, 20), 42);
    const int marker = 321;
    std::memcpy(&map->events[47].unk_30[16], &marker, sizeof(marker));
    EXPECT_EQ(chronicle::dungeon::RetailDummyPart(*map, -1, 0), marker);
}
