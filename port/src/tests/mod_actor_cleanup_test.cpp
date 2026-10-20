#include <gtest/gtest.h>
#include "mods/actor_cleanup.hpp"
#include "mods/entity_events.hpp"
#include "chronicle/monster_limits.hpp"
#include "collisiondata.hpp"
#include "shot_effect_pack.hpp"
#include <memory>
#include <cstring>

TEST(ModActorCleanup, RemovesOnlyMatchingReferencesIncludingExpandedSlotAndArrayTails) {
    auto collision = std::make_unique<CCollisionData>();
    auto shots = std::make_unique<CSHOT_EFFECT_PACK>();
    std::memset(collision.get(), 0, sizeof(*collision));
    std::memset(shots.get(), 0, sizeof(*shots));
    constexpr unsigned slot = chronicle::monsters::SlotCapacity - 1;
    collision->active[0] = collision->active[1] = collision->active[95] = 1;
    collision->hit[0].owner = slot * 5 + 200;
    collision->hit[95].monster_no = slot;
    collision->hit[1].owner = 205; collision->hit[1].monster_no = 1;
    shots->effect[0].active[0] = 1; shots->effect[0].user_id_2[0] = slot;
    shots->effect[4].active[7] = 1; shots->effect[4].user_id_2[7] = slot;
    shots->effect[4].active[6] = 1; shots->effect[4].user_id_2[6] = 1;
    const auto result = mods::ClearActorCombatReferences(collision.get(), shots.get(), slot);
    EXPECT_EQ(result.hits, 2); EXPECT_EQ(result.projectiles, 2);
    EXPECT_EQ(collision->active[0], 0); EXPECT_EQ(collision->active[95], 0);
    EXPECT_EQ(collision->active[1], 1); EXPECT_EQ(shots->effect[4].active[6], 1);
    EXPECT_EQ(shots->effect[0].active[0], 0); EXPECT_EQ(shots->effect[4].active[7], 0);
    const auto again = mods::ClearActorCombatReferences(collision.get(), shots.get(), slot);
    EXPECT_EQ(again.hits, 0); EXPECT_EQ(again.projectiles, 0);
    EXPECT_EQ(mods::ClearActorCombatReferences(collision.get(), shots.get(), chronicle::monsters::SlotCapacity).hits, 0);
    EXPECT_EQ(collision->active[1], 1);
    EXPECT_EQ(mods::ClearActorCombatReferences(nullptr, nullptr, slot).projectiles, 0);
}
TEST(ModActorCleanup, DeathCancellationMatchesUnitAndGenerationAndKeepsOrder) {
    mods::EnemyDeathQueue queue; queue.Enable(true);
    // Opaque identities only; the queue never dereferences units.
    int one, two;
    auto *first = reinterpret_cast<CMonstorUnit *>(&one), *second = reinterpret_cast<CMonstorUnit *>(&two);
    mods::EnemyDeath a; a.unit = first; a.entity = 256;
    auto b = a; b.entity = 512;
    auto c = a; c.unit = second;
    queue.Push(a); queue.Push(b); queue.Push(c); queue.Push(a);
    queue.CancelEntity(first, 256);
    ASSERT_EQ(queue.Pending().size(), 2);
    EXPECT_EQ(queue.Pending()[0].entity, 512);
    EXPECT_EQ(queue.Pending()[1].unit, second);
    EXPECT_EQ(queue.Dropped(), 0);
    EXPECT_TRUE(queue.Enabled());
}
