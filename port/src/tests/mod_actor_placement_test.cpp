#include <gtest/gtest.h>
#include "mods/actor_spawn.hpp"
#include "dungeonmap.hpp"
#include "dungeonparts.hpp"
#include "dun/gameloop.hpp"
#include <memory>
#include <limits>

TEST(ModActorPlacement, RotationHeightRadiusAndExpandedBoundsAreReadOnly) {
    auto map = std::make_unique<CDungeonMap>(); map->port_grid_extent = 20;
    for (auto &cell : map->cells) cell.parts_no = MAP_PARTS_NONE;
    ITEM_FREE_AREA areas[2]{};
    areas[0].parts_no = 0; areas[0].rect_num = 1;
    areas[0].rect[0] = {-2, 1, -4, 2, 1, 4}; areas[1].parts_no = -1;
    auto &cell = map->cells[3 + 4 * 48]; cell.parts_no = 0; cell.direction = 0;
    std::string error;
    srand(9); const auto expected = rand(); srand(9);
    EXPECT_TRUE(mods::ValidateActorPlacement(*map, areas, {480, 10, 665}, 5, error));
    EXPECT_FALSE(mods::ValidateActorPlacement(*map, areas, {505, 10, 640}, 5, error));
    cell.direction = 1;
    EXPECT_TRUE(mods::ValidateActorPlacement(*map, areas, {505, 10, 640}, 5, error));
    EXPECT_FALSE(mods::ValidateActorPlacement(*map, areas, {480, 20, 640}, 5, error));
    EXPECT_FALSE(mods::ValidateActorPlacement(*map, areas, {480, 10, 640}, 21, error));
    EXPECT_EQ(rand(), expected);
    cell.parts_no = MAP_PARTS_NONE;
    auto &far = map->cells[46 + 46 * 48]; far.parts_no = 0; far.direction = 0;
    EXPECT_FALSE(mods::ValidateActorPlacement(*map, areas, {7360, 10, 7360}, 5, error));
    map->port_grid_extent = 48;
    EXPECT_TRUE(mods::ValidateActorPlacement(*map, areas, {7360, 10, 7360}, 5, error));
}
TEST(ModActorPlacement, RejectsUnknownOrMalformedGeometryWithoutUnboundedSentinelReads) {
    auto map = std::make_unique<CDungeonMap>(); map->port_grid_extent = 20;
    for (auto &cell : map->cells) cell.parts_no = MAP_PARTS_NONE;
    std::string error; ITEM_FREE_AREA area{};
    EXPECT_FALSE(mods::ValidateActorPlacement(*map, {}, {}, 0, error));
    EXPECT_FALSE(mods::ValidateActorPlacement(*map, {&area, 1}, {}, 0, error));
    area.rect_num = 5;
    EXPECT_FALSE(mods::ValidateActorPlacement(*map, {&area, 1}, {}, 0, error));
    EXPECT_TRUE(mods::LoadedActorPlacementAreas(-1).empty());
    EXPECT_TRUE(mods::LoadedActorPlacementAreas(7).empty());
    for (int dungeon = 0; dungeon < 7; ++dungeon) {
        const auto areas = mods::LoadedActorPlacementAreas(dungeon);
        ASSERT_FALSE(areas.empty()); EXPECT_EQ(areas.back().parts_no, -1);
    }
    EXPECT_FALSE(mods::ValidateActorPlacement(*map, mods::LoadedActorPlacementAreas(0),
        {std::numeric_limits<float>::infinity(), 0, 0}, 0, error));
}

TEST(ModActorPlacement, NativeGeneratedPointsMatchBoundedGeometryAcrossAllDungeonTables) {
    auto map = std::make_unique<CDungeonMap>(); map->port_grid_extent = 20;
    for (auto &cell : map->cells) cell.parts_no = MAP_PARTS_NONE;
    auto &cell = map->cells[3 + 3 * 48];
    const int previous = selectMapNo;
    std::string error;
    for (int dungeon = 0; dungeon < 7; ++dungeon) {
        selectMapNo = dungeon;
        const auto areas = mods::LoadedActorPlacementAreas(dungeon);
        ASSERT_FALSE(areas.empty());
        cell.parts_no = areas[0].parts_no;
        for (int turn = 0; turn < 4; ++turn) {
            cell.direction = turn;
            for (int seed = 0; seed < 8; ++seed) {
                srand(seed); float point[4]{};
                ASSERT_GT(SearchiDoPutArea(map->cells, 0, 0, 20, 20, point), 0);
                EXPECT_TRUE(mods::ValidateActorPlacement(*map, areas, {point[0], point[1], point[2]}, 0, error))
                    << "dungeon=" << dungeon << " turn=" << turn << " seed=" << seed << " " << error;
            }
        }
    }
    selectMapNo = previous;
}
