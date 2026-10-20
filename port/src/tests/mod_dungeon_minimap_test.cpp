#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>

#include "../dungeon_minimap.hpp"
#include "dngstatusdata.hpp"
#include "dun/gameloop.hpp"
#include "dungeonmap.hpp"
#include "tex_fixture.hpp"

TEST(ModDungeonMinimap, FullExpandedGridFitsAndFarMarkersShareTheTileProjection) {
    auto map = std::make_unique<CDungeonMap>();
    map->port_grid_extent = 48;
    for (auto &cell : map->cells) {
        cell.parts_no = 0;
    }
    std::fill(std::begin(map->mask), std::end(map->mask), 1);
    std::array<float, 4> pos{43 * 160.0f, 0, 43 * 160.0f, 1};
    auto                &box = map->boxes[0];
    box.used = box.closed = 1;
    std::copy(pos.begin(), pos.end(), box.pos);
    map->trap_circle[0].state = 1;
    std::copy(pos.begin(), pos.end(), map->trap_circle[0].pos);
    map->atra[0].used = 1;
    std::copy(pos.begin(), pos.end(), map->atra[0].pos);
    std::vector<unsigned char> before(sizeof(*map));
    std::memcpy(before.data(), static_cast<const void *>(map.get()), before.size());
    const auto original_pos = pos;
    srand(19);
    const int next_rng = rand();
    srand(19);
    const auto sprites = ExpandedDungeonMiniMap(*map, pos.data(), 0, false, false);
    ASSERT_EQ(sprites.size(), 2308u);
    for (int i = 0; i < 2304; ++i) {
        const auto &s = sprites[i];
        EXPECT_EQ(s.kind, DungeonMiniMapKind::Tile);
        EXPECT_GE(s.screen.x, 380);
        EXPECT_GE(s.screen.y, 64);
        EXPECT_LE(s.screen.x + s.screen.width, 620);
        EXPECT_LE(s.screen.y + s.screen.height, 304);
        EXPECT_EQ(s.screen.width, 5);
        EXPECT_EQ(s.screen.height, 5);
        EXPECT_EQ(s.tone, 0x60);
        EXPECT_EQ(s.alpha, 0x40);
    }
    EXPECT_EQ(sprites[2303].screen.x, 615);
    EXPECT_EQ(sprites[2303].screen.y, 299);
    const auto &player = sprites[2304];
    EXPECT_EQ(player.kind, DungeonMiniMapKind::Player);
    EXPECT_EQ(player.screen.x, 597);
    EXPECT_EQ(player.screen.y, 281);
    for (int i = 2305; i < 2308; ++i) {
        EXPECT_EQ(sprites[i].screen.x + 4, player.screen.x);
        EXPECT_EQ(sprites[i].screen.y + 4, player.screen.y);
    }
    EXPECT_EQ(std::memcmp(static_cast<const void *>(map.get()), before.data(), before.size()), 0);
    EXPECT_EQ(pos, original_pos);
    EXPECT_EQ(rand(), next_rng);
}

TEST(ModDungeonMinimap, RevealFlagsVisibilityAndBoundsAreIndependent) {
    auto map = std::make_unique<CDungeonMap>();
    map->port_grid_extent = 48;
    for (auto &cell : map->cells) {
        cell.parts_no = MAP_PARTS_NONE;
    }
    map->cells[0].parts_no = 0;
    map->mask[0] = 1;
    constexpr int far = 47 + 47 * 48;
    map->cells[far].parts_no = 0;
    map->cells[far].direction = 3;
    auto &box = map->boxes[0];
    box.used = box.closed = 1;
    box.pos[0] = box.pos[2] = 47 * 160.0f;
    const float pos[4]{0, 0, 0, 1};
    auto        sprites = ExpandedDungeonMiniMap(*map, pos, 1, false, false);
    EXPECT_EQ(sprites.size(), 2u); // Discovered tile and player.
    sprites = ExpandedDungeonMiniMap(*map, pos, 1, true, false);
    ASSERT_EQ(sprites.size(), 3u);
    EXPECT_EQ(sprites[1].tone, 0x40);
    EXPECT_EQ(sprites[1].atlas.x, 48);
    sprites = ExpandedDungeonMiniMap(*map, pos, 1, false, true);
    ASSERT_EQ(sprites.size(), 3u);
    EXPECT_EQ(sprites.back().kind, DungeonMiniMapKind::Chest);
    map->cells[far].parts_no = 72;
    EXPECT_EQ(ExpandedDungeonMiniMap(*map, pos, 1, false, true).size(), 2u);
    map->cells[far].parts_no = 0;
    box.closed = 0;
    EXPECT_EQ(ExpandedDungeonMiniMap(*map, pos, 1, false, true).size(), 2u);
    box.closed = 1;
    for (float invalid : {-1000.f, 100000.f, std::numeric_limits<float>::quiet_NaN()}) {
        box.pos[0] = invalid;
        EXPECT_EQ(ExpandedDungeonMiniMap(*map, pos, 1, false, true).size(), 2u);
    }
    EXPECT_TRUE(ExpandedDungeonMiniMap(*map, pos, 3, true, true).empty());
    map->port_grid_extent = 20;
    EXPECT_TRUE(ExpandedDungeonMiniMap(*map, pos, 0, true, true).empty());
}

TEST(ModDungeonMinimap, NativeDrawUsesFarTileAndMarkerCoordinates) {
    texfix::TexEnv env;
    texfix::Bytes  white(128 * 128 * 4), green(white.size());
    for (std::size_t i = 0; i < white.size(); i += 4) {
        white[i] = white[i + 1] = white[i + 2] = 255;
        white[i + 3] = green[i + 3] = 0x80;
        green[i + 1] = 255;
    }
    auto img = texfix::Img({
        {"minimap1", texfix::Tim2({TIM2_RGB32, 128, 128, {white}, {}, 0})},
        {"itempack", texfix::Tim2({TIM2_RGB32, 128, 128, {green}, {}, 0})}
    });
    TexManager.BeginEnterTextureBlock(1);
    TexManager.EnterIMGFile(img.data(), 1, 0, 0);
    TexManager.EndEnterTextureBlock(1);
    CDngStatusData status{};
    auto          *old_status = UserStatus;
    UserStatus = &status;
    const int old_map = BtEquipMap, old_crystal = BtEquipMasuisyou;
    BtEquipMap = BtEquipMasuisyou = 0;
    auto map = std::make_unique<CDungeonMap>();
    map->port_grid_extent = 48;
    for (auto &cell : map->cells) {
        cell.parts_no = MAP_PARTS_NONE;
    }
    constexpr int far = 47 + 47 * 48;
    map->cells[far].parts_no = 0;
    map->mask[far] = 1;
    float pos[4]{};
    env.gfx.Frame({0, 0, 0, 255}, [&] { map->DrawMiniMap(pos, 0); });
    auto tile = env.gfx.Pixel(617, 301);
    EXPECT_GT(tile[0], 0);
    EXPECT_EQ(tile[0], tile[1]);
    EXPECT_EQ(tile[1], tile[2]);
    EXPECT_TRUE(env.gfx.PixelNear(625, 310, 0, 0, 0));
    auto &box = map->boxes[0];
    box.used = box.closed = 1;
    box.pos[0] = box.pos[2] = 47 * 160.0f;
    env.gfx.Frame({0, 0, 0, 255}, [&] { map->DrawMiniMap(pos, 0); });
    const auto marker = env.gfx.Pixel(617, 301);
    EXPECT_GT(marker[1], marker[0]);
    EXPECT_GT(marker[1], marker[2]);
    EXPECT_EQ(BtEquipMap, 0);
    EXPECT_EQ(BtEquipMasuisyou, 0);
    if (const char *output = std::getenv("DC_MINIMAP_AUDIT")) {
        for (int z = 0; z < 48; ++z) {
            for (int x = 0; x < 48; ++x) {
                const int i = x + z * 48;
                map->cells[i].parts_no = (x % 8 < 5 && z % 8 < 5) ? 0 : MAP_PARTS_NONE;
                map->mask[i] = 1;
            }
        }
        map->cells[far].parts_no = 0;
        pos[0] = pos[2] = 43 * 160.0f;
        env.gfx.Frame({0, 0, 0, 255}, [&] { map->DrawMiniMap(pos, 0); });
        gfx::WritePng(output, env.gfx.pixels.data(), env.gfx.width, env.gfx.height);
    }
    BtEquipMap = old_map;
    BtEquipMasuisyou = old_crystal;
    UserStatus = old_status;
}
