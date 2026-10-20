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
    const int expanded_tail = 999;
    std::memcpy(&map->events[383].unk_30[16], &expanded_tail, sizeof(expanded_tail));
    EXPECT_EQ(chronicle::dungeon::RetailDummyPart(*map, -1, 0), marker);
}

TEST(ModDungeonCapacity, ExpandedSceneryStaysInsideGridAndAvoidsFarEntrance) {
    using namespace chronicle::dungeon;
    auto       map = std::make_unique<CDungeonMap>();
    BuildScope scope({64, 46, 3, 6});
    for (auto &cell : map->cells) {
        cell.parts_no = MAP_PARTS_NONE;
    }
    EXPECT_NE(BuildDummyPart(*map, -1, 0), MAP_PARTS_NONE);
    EXPECT_NE(BuildDummyPart(*map, 48, 0), MAP_PARTS_NONE);
    EXPECT_NE(BuildDummyPart(*map, 0, -1), MAP_PARTS_NONE);
    EXPECT_NE(BuildDummyPart(*map, 0, 48), MAP_PARTS_NONE);
    EXPECT_NE(BuildDummyPart(*map, 47, 47), MAP_PARTS_NONE);
    EXPECT_EQ(BuildDummyPart(*map, 46, 47), MAP_PARTS_NONE);
    map->cells[42 + 42 * GridStride].parts_no = MAP_PARTS_URA_ENTRANCE_NORTH;
    bool far = false;
    for (int seed = 1; seed <= 64; ++seed) {
        srand(seed);
        map->buildDummyModel();
        ASSERT_EQ(map->dummy_num, 8);
        for (int n = 0; n < map->dummy_num; ++n) {
            const int   kind = map->dummy_model[n];
            const float offset = kind == 0 ? 80.0f : kind == 1 ? 160.0f
                                                               : 240.0f;
            const int   x = static_cast<int>((map->dummy_pos[n][0] - offset) / 160.0f);
            const int   z = static_cast<int>((map->dummy_pos[n][2] - offset) / 160.0f);
            const int   side = kind == 2 ? 3 : 2;
            EXPECT_GE(x, 0);
            EXPECT_GE(z, 0);
            EXPECT_LE(x + side, GridStride - 1);
            EXPECT_LE(z + side, GridStride);
            const float dx = map->dummy_pos[n][0] - (80 + 160 * 42);
            const float dz = map->dummy_pos[n][2] - (80 + 160 * 42);
            EXPECT_GE(dx * dx + dz * dz, 160.0f * 160.0f * (kind + 2) * (kind + 2));
            far |= x >= 20 || z >= 20;
        }
    }
    EXPECT_TRUE(far);
    for (auto &cell : map->cells) {
        cell.parts_no = 0;
    }
    map->buildDummyModel();
    EXPECT_EQ(map->dummy_num, 0);
}
