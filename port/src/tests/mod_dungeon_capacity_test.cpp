#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <memory>

#include "dungeonmap.hpp"

TEST(ModDungeonCapacity, ExpandedRoomVisibilityStorageClearsWithoutChangingCellsOrRng) {
    static_assert(chronicle::dungeon::RoomCapacity == 64);
    auto map = std::make_unique<CDungeonMap>();
    ASSERT_EQ(std::size(map->rooms), 64u);
    ASSERT_EQ(std::size(map->room_seen), 64u);
    ASSERT_EQ(std::size(map->cells), 400u);
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
    EXPECT_EQ(map->mask[4 + 4 * 20], 1);
}
