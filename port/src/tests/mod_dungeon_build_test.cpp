#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <vector>

#include "chronicle/dungeon_build.hpp"
#include "dngstatusdata.hpp"
#include "dun/gameloop.hpp"
#include "dungeonmap.hpp"

TEST(ModDungeonBuild, ScopeRestoresDefaultsAfterRejectedPacking) {
    using namespace chronicle::dungeon;
    CDngStatusData status{};
    UserStatus = &status;
    status.res_limit_zone_current = -1;
    selectMapNo = 0;
    auto map = std::make_unique<CDungeonMap>();
    NowDngMap = map.get();
    srand(1);
    EXPECT_THROW(([](CDungeonMap &map) {
                     BuildScope scope({2, 8, 6, 6});
                     map.buildRandomMap(6, 1);
                 })(*map),
                 std::runtime_error);
    EXPECT_FALSE(CustomBuild());
    EXPECT_EQ(map->GridExtent(), 20);
    EXPECT_EQ(BuildExtent(), 20);
    EXPECT_EQ(BuildArea(), 16);
    UserStatus = nullptr;
    NowDngMap = nullptr;
}

TEST(ModDungeonBuild, ExpandedGenerationUsesRequestedRoomSizesAndStorage) {
    using namespace chronicle::dungeon;
    CDngStatusData status{};
    UserStatus = &status;
    status.res_limit_zone_current = -1;
    status.cur_floor = 10;
    status.party_size = 6;
    for (unsigned seed = 1; seed <= 192; ++seed) {
        constexpr int dungeons[] = {0, 1, 2, 4, 5, 6};
        selectMapNo = dungeons[(seed - 1) / 32];
        SCOPED_TRACE(seed);
        auto map = std::make_unique<CDungeonMap>();
        NowDngMap = map.get();
        srand(seed);
        {
            BuildScope scope({64, 46, 3, 6});
            ASSERT_NO_THROW(map->buildRandomMap(6, 1));
        }
        EXPECT_FALSE(CustomBuild());
        EXPECT_EQ(map->GridExtent(), 48);
        if (seed == 1) {
            auto copied = std::make_unique<CDungeonMap>(*map);
            EXPECT_EQ(copied->GridExtent(), 48);
            copied->initalize();
            EXPECT_EQ(copied->GridExtent(), 20);
            EXPECT_EQ(map->GridExtent(), 48);
        }
        EXPECT_GT(map->room_num, 16);
        EXPECT_LE(map->room_num, 64);
        bool outside_retail = false;
        for (int i = 0; i < map->room_num; ++i) {
            const auto &r = map->rooms[i];
            EXPECT_GE(r.width, 3);
            EXPECT_LE(r.width, 6);
            EXPECT_GE(r.height, 3);
            EXPECT_LE(r.height, 6);
            EXPECT_GE(r.x, 1);
            EXPECT_GE(r.y, 1);
            EXPECT_LT(r.x + r.width, GridStride);
            EXPECT_LT(r.y + r.height, GridStride);
            outside_retail |= r.x >= 20 || r.y >= 20;
        }
        EXPECT_TRUE(outside_retail);
        int down = 0, up = 0;
        for (const auto &cell : map->cells) {
            down += cell.parts_no == MAP_PARTS_STAIR_DOWN;
            up += cell.parts_no == MAP_PARTS_STAIR_UP;
        }
        EXPECT_EQ(down, 1);
        EXPECT_EQ(up, 1);
        // Structural connectivity, not a replacement for collision traversal.
        std::array<bool, GridCells> reached{};
        std::vector<int>            pending;
        for (int i = 0; i < GridCells; ++i) {
            if (map->cells[i].parts_no == MAP_PARTS_STAIR_DOWN) {
                pending.push_back(i);
                reached[i] = true;
                break;
            }
        }
        for (std::size_t next = 0; next < pending.size(); ++next) {
            const int index = pending[next], x = index % GridStride, y = index / GridStride;
            for (const auto [dx, dy] : {
                     std::pair{-1, 0 },
                     std::pair{1,  0 },
                     std::pair{0,  -1},
                     std::pair{0,  1 }
            }) {
                const int nx = x + dx, ny = y + dy;
                if (nx < 0 || nx >= GridStride || ny < 0 || ny >= GridStride) {
                    continue;
                }
                const int adjacent = nx + ny * GridStride;
                const int part = map->cells[adjacent].parts_no;
                if (!reached[adjacent] && part != MAP_PARTS_NONE && part != MAP_PARTS_URA_ROAD) {
                    reached[adjacent] = true;
                    pending.push_back(adjacent);
                }
            }
        }
        for (int i = 0; i < map->room_num; ++i) {
            const auto &r = map->rooms[i];
            EXPECT_TRUE(reached[r.x + r.width / 2 + (r.y + r.height / 2) * GridStride]) << "room " << i;
        }
        for (int i = 0; i < GridCells; ++i) {
            if (map->cells[i].parts_no == MAP_PARTS_STAIR_UP) {
                EXPECT_TRUE(reached[i]);
            }
        }
    }
    UserStatus = nullptr;
    NowDngMap = nullptr;
}
