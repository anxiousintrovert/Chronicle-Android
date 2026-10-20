#include <gtest/gtest.h>
#include "mods/actor_cleanup.hpp"
#include "mods/entity_events.hpp"
#include "mods/entity_tracking.hpp"
#include "mods/entity_reactions.hpp"
#include "monstorunit.hpp"
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

TEST(ModActorCleanup, OwnedRetirementPreservesOtherActorsAndNeverDispatchesKillRewards) {
    auto unit = std::make_unique<CMonstorUnit>(); unit->CleanViewMonstor(0);
    for (int slot : {0, 1}) {
        unit->monster[slot].state = 2; unit->monster[slot].hp = 10;
        unit->monster[slot].kind = 0; unit->monster[slot].attachment_kind = 0;
    }
    unit->alive_count = 2;
    auto &handles = mods::TrackEntities(*unit);
    const auto handle = handles.Get(0), other = handles.Get(1);
    mods::ActorOwners owners; std::string error;
    ASSERT_TRUE(owners.Claim(handles, handle, "alpha", "guard", error));
    const auto before = unit->monster[0];
    EXPECT_FALSE(mods::RetireOwnedActor(unit.get(), owners, handle, "beta", nullptr, nullptr, -1, -1, -1, error));
    EXPECT_EQ(std::memcmp(&unit->monster[0], &before, sizeof(before)), 0);
    unit->monster[0].event_flag2 = 7;
    EXPECT_FALSE(mods::RetireOwnedActor(unit.get(), owners, handle, "alpha", nullptr, nullptr, -1, -1, -1, error));
    unit->monster[0].event_flag2 = -1;
    unit->sound[0].sequence_id = 2800;
    EXPECT_FALSE(mods::RetireOwnedActor(unit.get(), owners, handle, "alpha", nullptr, nullptr, -1, -1, -1, error));
    EXPECT_EQ(unit->alive_count, 2); EXPECT_TRUE(handles.Resolve(handle));
    unit->sound[0].sequence_id = -1;
    ASSERT_TRUE(mods::CanQueueEntityReactions(unit.get()));
    mods::QueueEntityReaction(unit.get(), 0, MONSTER_DMG_HIT);
    mods::EnemyDeaths().Enable(true);
    mods::EnemyDeath event; event.unit = unit.get(); event.entity = handle;
    mods::EnemyDeaths().Push(event); event.entity = other; mods::EnemyDeaths().Push(event);
    ASSERT_TRUE(mods::RetireOwnedActor(unit.get(), owners, handle, "alpha", nullptr, nullptr, -1, -1, -1, error)) << error;
    EXPECT_EQ(unit->alive_count, 1); EXPECT_EQ(unit->monster[0].state, -1); EXPECT_EQ(unit->monster[0].hp, 0);
    EXPECT_EQ(unit->monster[1].hp, 10); EXPECT_EQ(unit->monster[1].state, 2);
    EXPECT_FALSE(handles.Resolve(handle)); EXPECT_TRUE(handles.Resolve(other)); EXPECT_EQ(owners.Size(), 0);
    EXPECT_EQ(mods::TakeEntityReaction(unit.get(), 0), MONSTER_DMG_NONE);
    ASSERT_EQ(mods::EnemyDeaths().Pending().size(), 1);
    EXPECT_EQ(mods::EnemyDeaths().Pending()[0].entity, other);
    EXPECT_FALSE(mods::RetireOwnedActor(unit.get(), owners, handle, "alpha", nullptr, nullptr, -1, -1, -1, error));
    EXPECT_EQ(unit->alive_count, 1);
    mods::EnemyDeaths().Enable(false); mods::StopTrackingEntities();
}

TEST(ModActorCleanup, OwnerRetentionRetriesFailuresWithoutLosingOrRetiringOtherOwners) {
    auto unit = std::make_unique<CMonstorUnit>(); unit->CleanViewMonstor(0);
    for (int slot : {0, 1, 2}) {
        auto &actor = unit->monster[slot];
        actor.state = 2; actor.hp = 10; actor.kind = 0; actor.attachment_kind = 0;
    }
    unit->alive_count = 3;
    mods::StopTrackingEntities(); auto &handles = mods::TrackEntities(*unit);
    const auto first = handles.Get(0), blocked = handles.Get(1), other = handles.Get(2);
    mods::ActorOwners owners; std::string error;
    ASSERT_TRUE(owners.Claim(handles, first, "alpha", "guard", error));
    ASSERT_TRUE(owners.Claim(handles, blocked, "alpha", "guard", error));
    ASSERT_TRUE(owners.Claim(handles, other, "beta", "guard", error));
    unit->sound[1].sequence_id = 2800;
    const std::vector<std::string> active{"beta"};
    const auto result = mods::RetainOwnedActors(unit.get(), owners, active, nullptr, nullptr, -1, -1, -1);
    EXPECT_EQ(result.retired, 1); EXPECT_EQ(result.retained, 1); EXPECT_FALSE(result.Complete());
    ASSERT_EQ(result.pending.size(), 1); EXPECT_EQ(result.pending[0].entity, blocked);
    EXPECT_EQ(result.pending[0].owner, "alpha"); EXPECT_FALSE(result.pending[0].reason.empty());
    EXPECT_EQ(unit->alive_count, 2); EXPECT_EQ(owners.Size(), 2);
    EXPECT_TRUE(owners.Find(handles, blocked)); EXPECT_TRUE(owners.Find(handles, other));
    EXPECT_FALSE(handles.Resolve(first)); EXPECT_EQ(unit->monster[2].hp, 10);
    // Repair native readiness; the same retained metadata drives the retry.
    unit->sound[1].sequence_id = -1;
    const auto retry = mods::RetainOwnedActors(unit.get(), owners, active, nullptr, nullptr, -1, -1, -1);
    EXPECT_TRUE(retry.Complete()); EXPECT_EQ(retry.retired, 1); EXPECT_EQ(retry.retained, 1);
    EXPECT_EQ(unit->alive_count, 1); EXPECT_EQ(owners.Size(), 1);
    const auto repeated = mods::RetainOwnedActors(unit.get(), owners, active, nullptr, nullptr, -1, -1, -1);
    EXPECT_EQ(repeated.retired, 0); EXPECT_EQ(unit->alive_count, 1);
    const auto all = mods::RetainOwnedActors(unit.get(), owners, {}, nullptr, nullptr, -1, -1, -1);
    EXPECT_TRUE(all.Complete()); EXPECT_EQ(all.retired, 1); EXPECT_EQ(unit->alive_count, 0);
    EXPECT_EQ(owners.Size(), 0); mods::StopTrackingEntities();
}

TEST(ModActorCleanup, LostTrackingAndNativeDeathDoNotSilentlyDiscardOwnership) {
    auto unit = std::make_unique<CMonstorUnit>(); unit->CleanViewMonstor(0);
    auto &actor = unit->monster[0]; actor.state = 2; actor.hp = 10;
    actor.kind = 0; actor.attachment_kind = 0; unit->alive_count = 1;
    mods::StopTrackingEntities(); auto &handles = mods::TrackEntities(*unit);
    mods::ActorOwners owners; std::string error; const auto handle = handles.Get(0);
    ASSERT_TRUE(owners.Claim(handles, handle, "alpha", "guard", error));
    actor.hp = 0; unit->alive_count = 0;
    auto pending = mods::RetainOwnedActors(unit.get(), owners, {}, nullptr, nullptr, -1, -1, -1);
    EXPECT_FALSE(pending.Complete()); EXPECT_EQ(owners.Size(), 1); EXPECT_EQ(unit->alive_count, 0);
    mods::StopTrackingEntities();
    pending = mods::RetainOwnedActors(unit.get(), owners, {}, nullptr, nullptr, -1, -1, -1);
    EXPECT_FALSE(pending.Complete()); EXPECT_EQ(owners.Size(), 1);
    EXPECT_EQ(pending.pending[0].reason, "actor unit is not currently tracked");
    // Restarting tracking issues a new identity even if native memory is still
    // occupied. The unresolved old record must not be mistaken for safe cleanup.
    mods::TrackEntities(*unit);
    pending = mods::RetainOwnedActors(unit.get(), owners, {}, nullptr, nullptr, -1, -1, -1);
    EXPECT_FALSE(pending.Complete()); EXPECT_EQ(owners.Size(), 1); EXPECT_EQ(actor.state, 2);
    EXPECT_EQ(pending.pending[0].entity, handle);
    owners.Clear(); mods::StopTrackingEntities();
    // Empty cleanup remains dormant even without a native unit.
    EXPECT_TRUE(mods::RetainOwnedActors(nullptr, owners, {}, nullptr, nullptr, -1, -1, -1).Complete());
}

TEST(ModActorCleanup, BoundOwnershipPinsTrackingUntilRetirementOrNativeLifetimeEnds) {
    struct Fixture {
        std::unique_ptr<CMonstorUnit> unit = std::make_unique<CMonstorUnit>();
        ~Fixture() { unit->CleanViewMonstor(0); mods::StopTrackingEntities(); }
    } fixture;
    auto &unit = *fixture.unit; unit.CleanViewMonstor(0);
    auto other = std::make_unique<CMonstorUnit>(); other->CleanViewMonstor(0);
    mods::StopTrackingEntities();
    EXPECT_THROW(mods::BindActorOwnership(unit), std::runtime_error);
    auto &handles = mods::TrackEntities(unit);
    auto &owners = mods::BindActorOwnership(unit);
    EXPECT_FALSE(mods::HasBoundActorOwnership(&unit));
    unit.monster[0].state = 2; unit.monster[0].hp = 10;
    unit.monster[0].kind = 0; unit.monster[0].attachment_kind = 0; unit.alive_count = 1;
    mods::EntitySlotConstructed(&unit, 0); const auto handle = handles.Get(0);
    std::string error; ASSERT_TRUE(owners.Claim(handles, handle, "alpha", "guard", error));
    EXPECT_TRUE(mods::HasBoundActorOwnership(&unit));
    mods::StopTrackingEntities(); EXPECT_EQ(mods::TrackedEntities(&unit), &handles);
    mods::ExpireEntitiesOutsideUnit(nullptr); EXPECT_TRUE(handles.Resolve(handle));
    EXPECT_THROW(mods::TrackEntities(*other), std::runtime_error);
    EXPECT_TRUE(owners.Find(handles, handle));
    // A different unit's native cleanup cannot release this ownership lease.
    other->CleanViewMonstor(0); EXPECT_TRUE(mods::HasBoundActorOwnership(&unit));
    auto result = mods::RetainOwnedActors(&unit, owners, {}, nullptr, nullptr, -1, -1, -1);
    ASSERT_TRUE(result.Complete()); EXPECT_EQ(result.retired, 1);
    mods::StopTrackingEntities(); EXPECT_EQ(mods::TrackedEntities(&unit), nullptr);
    // Native replacement is explicit proof that the previous slot lifetime ended.
    mods::TrackEntities(unit); mods::BindActorOwnership(unit);
    unit.monster[0].state = 2; unit.monster[0].hp = 10;
    mods::EntitySlotConstructed(&unit, 0);
    ASSERT_TRUE(owners.Claim(handles, handles.Get(0), "alpha", "guard", error));
    mods::EntitySlotConstructed(&unit, 0);
    EXPECT_EQ(owners.Size(), 0); EXPECT_FALSE(mods::HasBoundActorOwnership(&unit));
    ASSERT_TRUE(owners.Claim(handles, handles.Get(0), "alpha", "guard", error));
    unit.CleanViewMonstor(0);
    EXPECT_EQ(owners.Size(), 0); EXPECT_FALSE(mods::HasBoundActorOwnership(&unit));
    EXPECT_NO_THROW(mods::TrackEntities(*other));
}
