#include <gtest/gtest.h>
#include <array>
#include <memory>
#include <stdexcept>
#include <vector>
#include "chronicle/monster_collision.hpp"
#include "chronicle/monster_population.hpp"
#include "chronicle/monster_scripts.hpp"
#include "chronicle/monster_audio.hpp"
#include "collisiondata.hpp"
#include "dataalloc.hpp"
#include "dun/gameloop.hpp"
#include "dungeonmap.hpp"
#include "frame.hpp"
#include "monstorunit.hpp"

namespace {
struct TerrainFixture {
    CFrame frame;
    CFrame player;
    CFrame *previous_player = CharaMain.frame;
    CCollisionMDT collision;
    std::vector<CCPolyBox> mesh;
    std::unique_ptr<CDungeonMap> map = std::make_unique<CDungeonMap>();
    explicit TerrainFixture(unsigned count = 1) : mesh(count) {
        CharaMain.frame = &player;
        player.SetPosition(0, 0, 0);
        for (auto &m : mesh) {
            m.poly.vertex[0][0] = -1;
            m.poly.vertex[1][0] = 1;
            m.poly.vertex[2][2] = 1;
            for (auto &v : m.poly.vertex) v[3] = 1;
            m.poly.attr.ground_kind = 17;
            for (int j = 0; j < 3; ++j) {
                m.box.min[j] = collision.min[j] = -1;
                m.box.max[j] = collision.max[j] = 1;
            }
        }
        collision.mesh = mesh.data();
        collision.mesh_count = int(mesh.size());
        frame.flags = 1;
        frame.collision = &collision;
        map->map_type = 1;
        for (auto &cell : map->cells) cell.parts_no = -1;
        for (auto &chest : map->boxes) chest.used = 0;
        map->atra_num = 0;
        map->box_collision_model = &frame;
        map->collision_model = &frame;
        map->parts[0].collision = &frame;
        map->parts[0].collision_turn = 0;
    }
    ~TerrainFixture() { CharaMain.frame = previous_player; }
};
}

TEST(ModMonsterCollision, PlacementDensityChangesOnlyForExplicitLargePopulations) {
    using chronicle::monsters::PlacementNeighborLimit;
    EXPECT_EQ(PlacementNeighborLimit(256, 16), 3);
    EXPECT_EQ(PlacementNeighborLimit(15, 256), 3);
    EXPECT_EQ(PlacementNeighborLimit(129, 129), 3);
    EXPECT_EQ(PlacementNeighborLimit(192, 192), 3);
    EXPECT_EQ(PlacementNeighborLimit(193, 193), 4);
    EXPECT_EQ(PlacementNeighborLimit(256, 256), 4);
    EXPECT_EQ(PlacementNeighborLimit(257, 256), 3);
}

TEST(ModMonsterCollision, TerrainMatchesNativeGeometryOrderAtRetailAndExpandedCoordinates) {
    TerrainFixture fixture;
    auto &map = *fixture.map;
    for (int extent : {20, 48}) {
        map.port_grid_extent = extent;
        const int cell = extent - 2;
        const float coord = 160.0f * cell;
        float position[4] = {coord, 0, coord, 1};
        fixture.player.SetPosition(position);
        map.cells[cell + cell * chronicle::dungeon::GridStride].parts_no = 0;
        map.cells[cell + cell * chronicle::dungeon::GridStride].direction = 1;
        map.boxes[0].used = 1;
        map.atra_num = 2;
        for (int i = 0; i < 3; ++i) {
            map.boxes[0].pos[i] = position[i];
            map.atra[0].pos[i] = position[i];
            map.atra[1].pos[i] = position[i] + 1000;
        }
        map.atra[0].used = map.atra[1].used = 1;
        std::array<CCPoly, 64> native{};
        const auto count = setCollisionData(&map, native.data(), position, 30, 5);
        ASSERT_EQ(count, 3);
        auto bounded = chronicle::monsters::ExpandedTerrainCollision(map, position, fixture.player.position);
        ASSERT_EQ(bounded.size(), unsigned(count));
        for (unsigned i = 0; i < bounded.size(); ++i) {
            for (int axis = 0; axis < 4; ++axis) {
                EXPECT_FLOAT_EQ(bounded[i].normal[axis], native[i].normal[axis]);
                for (int v = 0; v < 3; ++v) EXPECT_FLOAT_EQ(bounded[i].vertex[v][axis], native[i].vertex[v][axis]);
            }
            EXPECT_EQ(bounded[i].attr.ground_kind, native[i].attr.ground_kind);
        }
    }
}

TEST(ModMonsterCollision, TerrainExceedsRetailBufferAndRejectsInvalidPartsBeforeTransforming) {
    TerrainFixture fixture(500);
    auto &map = *fixture.map;
    map.cells[0].parts_no = 0;
    map.cells[0].direction = 0;
    float position[4] = {0, 0, 0, 1};
    auto polys = chronicle::monsters::ExpandedTerrainCollision(map, position, position);
    EXPECT_EQ(polys.size(), 500);
    fixture.frame.SetPosition(123, 0, 0);
    map.cells[1].parts_no = 72;
    EXPECT_THROW(chronicle::monsters::ExpandedTerrainCollision(map, position, position), std::runtime_error);
    EXPECT_FLOAT_EQ(fixture.frame.position[0], 123);
    map.cells[1].parts_no = -1;
    fixture.collision.mesh_count = 65537;
    EXPECT_THROW(chronicle::monsters::ExpandedTerrainCollision(map, position, position), std::runtime_error);
    EXPECT_FLOAT_EQ(fixture.frame.position[0], 123);
    map.atra_num = 9;
    EXPECT_THROW(chronicle::monsters::ExpandedTerrainCollision(map, position, position), std::invalid_argument);
}

TEST(ModMonsterCollision, FloorPreflightIncludesDistantTerrainAndPreservesUnitOnFailure) {
    TerrainFixture fixture;
    auto unit = std::make_unique<CMonstorUnit>();
    unit->collision = &fixture.frame;
    unit->monster[0].state = 2;
    unit->monster[0].hp = 37;
    const auto allocated = chronicle::monsters::AllocatedExtraScriptSlots();
    fixture.map->cells[47 + 47 * chronicle::dungeon::GridStride].parts_no = 72;
    fixture.map->port_grid_extent = 48;
    EXPECT_THROW(chronicle::monsters::PrepareSlotsForFloor(*unit, *fixture.map, 256, 0), std::runtime_error);
    EXPECT_EQ(unit->SlotLimit(), 16u);
    EXPECT_EQ(unit->monster[0].state, 2);
    EXPECT_EQ(unit->monster[0].hp, 37);
    EXPECT_EQ(chronicle::monsters::AllocatedExtraScriptSlots(), allocated);
    // No-mod loading must not inspect invalid or absent expansion resources.
    EXPECT_NO_THROW(chronicle::monsters::PrepareSlotsForFloor(*unit, *fixture.map, 16, 0));
    EXPECT_EQ(unit->monster[0].state, 2);
    fixture.map->cells[47 + 47 * chronicle::dungeon::GridStride].parts_no = -1;
    fixture.collision.mesh_count = 257;
    EXPECT_THROW(chronicle::monsters::PrepareSlotsForFloor(*unit, *fixture.map, 256, 0), std::runtime_error);
    EXPECT_EQ(unit->monster[0].hp, 37);
    EXPECT_EQ(unit->SlotLimit(), 16u);
    fixture.collision.mesh_count = 1;
    std::array<CFrame, 128> children;
    for (unsigned i = 0; i + 1 < children.size(); ++i) children[i].brother = &children[i + 1];
    fixture.frame.child = children.data();
    EXPECT_THROW(chronicle::monsters::PrepareSlotsForFloor(*unit, *fixture.map, 256, 0), std::runtime_error);
    EXPECT_EQ(unit->SlotLimit(), 16u);
    fixture.frame.child = nullptr;
}

TEST(ModMonsterCollision, FloorBoundaryInitializesExpandedSlotsAndClearsThemBeforeShrink) {
    TerrainFixture fixture;
    auto unit = std::make_unique<CMonstorUnit>();
    unit->collision = &fixture.frame;
    chronicle::monsters::PrepareSlotsForFloor(*unit, *fixture.map, 256, 0);
    EXPECT_EQ(unit->SlotLimit(), 256u);
    for (unsigned i = 16; i < 256; ++i) {
        EXPECT_EQ(unit->monster[i].state, -1);
        ASSERT_NE(unit->script[i], nullptr);
        unit->monster[i].state = 2;
        unit->script_state[i] = 1;
        unit->script[i]->Alloc(1);
    }
    auto *last_script = unit->script[255];
    // Retiring an expanded floor must succeed even if the next floor's
    // resources are not suitable for expanded populations.
    fixture.map->map_type = 0;
    chronicle::monsters::PrepareSlotsForFloor(*unit, *fixture.map, 16, 1);
    EXPECT_EQ(unit->SlotLimit(), 16u);
    EXPECT_EQ(unit->current_monster, 0);
    EXPECT_EQ(last_script->used, 0);
    for (unsigned i = 16; i < 256; ++i) {
        EXPECT_EQ(unit->monster[i].state, -1);
        EXPECT_EQ(unit->script_state[i], 0);
        EXPECT_EQ(unit->script[i], nullptr);
    }
}

TEST(ModMonsterCollision, CoordinatedPopulationResetRetiresHitsScriptsAndAudioTogether) {
    TerrainFixture fixture;
    auto unit = std::make_unique<CMonstorUnit>();
    auto hits = std::make_unique<CCollisionData>();
    unit->collision = &fixture.frame;
    struct Reset {
        ~Reset() { chronicle::monsters::ResetExpandedPopulation(); }
    } reset;
    chronicle::monsters::PreparePopulationForFloor(*unit, *fixture.map, *hits, 32, 0);
    EXPECT_EQ(unit->SlotLimit(), 32u);
    EXPECT_GT(hits->HitLimit(), 96u);
    EXPECT_EQ(chronicle::monsters::SoundSequenceLimit(), 64u);
    EXPECT_TRUE(chronicle::monsters::ExpandedEffectVoicesActive());
    unit->monster[31].state = 2;
    hits->active[hits->HitLimit() - 1] = 1;
    chronicle::monsters::ResetExpandedPopulation();
    EXPECT_EQ(unit->SlotLimit(), 16u);
    EXPECT_EQ(unit->monster[31].state, -1);
    EXPECT_EQ(unit->script[31], nullptr);
    EXPECT_EQ(hits->HitLimit(), 96u);
    EXPECT_EQ(chronicle::monsters::SoundSequenceLimit(), 32u);
    EXPECT_FALSE(chronicle::monsters::ExpandedEffectVoicesActive());
}

TEST(ModMonsterCollision, DensePopulationPreservesTerrainAndExceedsOldBufferSafely) {
    auto unit = std::make_unique<CMonstorUnit>();
    unit->port_slot_limit = 256;
    unit->current_monster = 0;
    CFrame frame;
    CCollisionMDT collision;
    std::array<CCPolyBox, 2> mesh{};
    for (auto &m : mesh) {
        m.poly.vertex[0][0] = -1;
        m.poly.vertex[1][0] = 1;
        m.poly.vertex[2][2] = 1;
        for (auto &v : m.poly.vertex) v[3] = 1;
        for (int j = 0; j < 3; ++j) {
            m.box.min[j] = -1;
            m.box.max[j] = 1;
            collision.min[j] = -1;
            collision.max[j] = 1;
        }
    }
    collision.mesh = mesh.data();
    collision.mesh_count = int(mesh.size());
    frame.flags = 1;
    frame.collision = &collision;
    unit->collision = &frame;
    for (unsigned i = 0; i < 256; ++i) {
        unit->monster[i].state = 2;
        unit->monster[i].collision_radius = 5;
        unit->chara[i][0].SetPosition(0, 0, 0);
    }
    CBoxVu0 box{};
    for (int j = 0; j < 3; ++j) {
        box.min[j] = -30;
        box.max[j] = 30;
    }
    std::array<CCPoly, 1> terrain{};
    terrain[0].attr.ground_kind = 17;
    auto polys = chronicle::monsters::ExpandedActorCollision(*unit, terrain, box);
    ASSERT_EQ(polys.size(), 511);
    EXPECT_EQ(polys[0].attr.ground_kind, 17);
    EXPECT_EQ(terrain[0].attr.ground_kind, 17);
    // Neither dormant storage nor the actor itself may add geometry.
    for (unsigned i = 1; i < 255; ++i) unit->monster[i].state = 0;
    polys = chronicle::monsters::ExpandedActorCollision(*unit, terrain, box);
    EXPECT_EQ(polys.size(), 3);
    unit->port_slot_limit = 32;
    polys = chronicle::monsters::ExpandedActorCollision(*unit, terrain, box);
    EXPECT_EQ(polys.size(), 1);
}

TEST(ModMonsterCollision, InvalidGeometryAndCyclesAreRejectedBeforeNativeWrites) {
    auto unit = std::make_unique<CMonstorUnit>();
    unit->port_slot_limit = 256;
    unit->current_monster = 0;
    for (auto &m : unit->monster) m.state = 0;
    unit->monster[255].state = 2;
    CFrame frame;
    CCollisionMDT collision;
    CCPolyBox placeholder{};
    collision.mesh = &placeholder;
    collision.mesh_count = 65537;
    frame.flags = 1;
    frame.collision = &collision;
    unit->collision = &frame;
    CBoxVu0 box{};
    EXPECT_THROW(chronicle::monsters::ExpandedActorCollision(*unit, {}, box), std::runtime_error);
    collision.mesh_count = 65536;
    unit->monster[254].state = 2;
    EXPECT_THROW(chronicle::monsters::ExpandedActorCollision(*unit, {}, box), std::runtime_error);
    collision.mesh_count = 0;
    frame.child = &frame;
    EXPECT_THROW(chronicle::monsters::ExpandedActorCollision(*unit, {}, box), std::runtime_error);
    frame.child = nullptr;
    unit->port_slot_limit = 16;
    EXPECT_THROW(chronicle::monsters::ExpandedActorCollision(*unit, {}, box), std::invalid_argument);
}
