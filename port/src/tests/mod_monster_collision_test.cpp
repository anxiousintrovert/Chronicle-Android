#include <gtest/gtest.h>
#include <array>
#include <memory>
#include <stdexcept>
#include "chronicle/monster_collision.hpp"
#include "frame.hpp"
#include "monstorunit.hpp"

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
