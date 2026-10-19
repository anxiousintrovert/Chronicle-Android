#include <gtest/gtest.h>

#include "mods/api.hpp"
#include "mods/entities.hpp"
#include "mods/entity_handles.hpp"
#include "mods/entity_tracking.hpp"
#include "mods/runtime.hpp"
#include "monstorunit.hpp"
#include "savedata.hpp"

TEST(ModEntityHandles, DormantRegistryCreatesNoIdentities) {
    mods::EntityHandles handles;
    for (unsigned i = 0; i < handles.Capacity; ++i) {
        EXPECT_EQ(handles.Construct(i), 0);
        EXPECT_EQ(handles.Get(i), 0);
    }
    EXPECT_FALSE(handles.Resolve(0));
    EXPECT_FALSE(handles.Resolve(16));
}

TEST(ModEntityHandles, CombatWritesStageInOrderAndRejectWithoutPublishing) {
    auto unit = std::make_unique<CMonstorUnit>();
    mods::StopTrackingEntities();
    unit->CleanViewMonstor(0);
    auto &actor = unit->monster[0];
    actor.state = 2;
    actor.hp = 10;
    actor.max_hp = 50;
    actor.revealed = -1;
    const auto              id = std::to_string(mods::TrackEntities(*unit).Get(0));
    mods::EntityTransaction transaction;
    std::string             error;
    const mods::GameCommand heal{
        "combat.heal", {{"entity", id}, {"amount", 25}}
    };
    const mods::GameCommand slow{
        "combat.status_add", {{"entity", id}, {"status", "slow"}, {"duration_frames", 90}}
    };
    ASSERT_TRUE(transaction.Stage(unit.get(), {heal, heal, slow}, error)) << error;
    EXPECT_EQ(actor.hp, 10);
    EXPECT_EQ(actor.slow_timer, 0);
    transaction.Commit();
    EXPECT_EQ(actor.hp, 50);
    EXPECT_EQ(actor.slow_timer, 90);
    actor.hp = 10;
    auto bad = slow;
    bad.arguments["duration_frames"] = 0;
    EXPECT_FALSE(transaction.Stage(unit.get(), {heal, bad}, error));
    transaction.Commit();
    EXPECT_EQ(actor.hp, 10);
    auto shorter = slow;
    shorter.arguments["duration_frames"] = 1;
    ASSERT_TRUE(transaction.Stage(unit.get(), {shorter}, error));
    transaction.Commit();
    EXPECT_EQ(actor.slow_timer, 90);
    actor.state = 1;
    EXPECT_FALSE(transaction.Stage(unit.get(), {heal}, error));
    mods::EntitySlotConstructed(unit.get(), 0);
    actor.state = 2;
    EXPECT_FALSE(transaction.Stage(unit.get(), {heal}, error));
    EXPECT_FALSE(transaction.Stage(nullptr, {heal}, error));
    mods::StopTrackingEntities();
}

TEST(ModEntityHandles, MixedEnemyAndSaveBatchRollsBackBothDomains) {
    auto unit = std::make_unique<CMonstorUnit>();
    auto save = std::make_unique<CSaveData>();
    mods::StopTrackingEntities();
    unit->CleanViewMonstor(0);
    auto &actor = unit->monster[0];
    actor.state = 2;
    actor.hp = 10;
    actor.max_hp = 50;
    actor.revealed = -1;
    const auto              id = std::to_string(mods::TrackEntities(*unit).Get(0));
    const mods::GameCommand heal{
        "combat.heal", {{"entity", id}, {"amount", 25}}
    };
    const mods::GameCommand flag{
        "story.flag_set", {{"id", 10}, {"value", 1}}
    };
    const mods::GameCommand bad{"fishing.spend", {{"amount", 9999}}};
    save->SetFishingPoint(0);
    save->SetGameFlag(10, 0);
    const auto          before = std::make_unique<CSaveData>(*save);
    mods::CommitEffects effects;
    std::string         error;
    EXPECT_FALSE(mods::ApplyEntityAndSave(*save, unit.get(), {heal, flag, bad}, 0, error, &effects, nullptr));
    EXPECT_EQ(actor.hp, 10);
    EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    EXPECT_EQ(effects.status_sounds, 0);
    ASSERT_TRUE(mods::ApplyEntityAndSave(*save, unit.get(), {flag, heal}, 0, error, &effects, nullptr)) << error;
    EXPECT_EQ(actor.hp, 35);
    EXPECT_EQ(save->GetGameFlag(10), 1);
    mods::StopTrackingEntities();
}

TEST(ModEntityHandles, CombatValidationRejectsUnsafeAmountsAndUnknownFields) {
    using Json = nlohmann::json;
    const Json heal = {
        {"entity", "16"},
        {"amount", 1   }
    };
    const Json status = {
        {"entity",          "16"  },
        {"status",          "stop"},
        {"duration_frames", 32767 }
    };
    EXPECT_TRUE(mods::ValidateOperation("combat.heal", heal).empty());
    EXPECT_TRUE(mods::ValidateOperation("combat.status_add", status).empty());
    for (const Json &bad : {Json(-1), Json(0), Json(1000001), Json(1.5), Json(true), Json("1"), Json(UINT64_MAX)}) {
        auto value = heal;
        value["amount"] = bad;
        EXPECT_FALSE(mods::ValidateOperation("combat.heal", value).empty());
    }
    auto value = status;
    value["duration_frames"] = 32768;
    EXPECT_FALSE(mods::ValidateOperation("combat.status_add", value).empty());
    value = status;
    value["status"] = "poison";
    EXPECT_FALSE(mods::ValidateOperation("combat.status_add", value).empty());
    value = heal;
    value["extra"] = 1;
    EXPECT_FALSE(mods::ValidateOperation("combat.heal", value).empty());
}

TEST(ModEntityHandles, QueriesRejectStaleActorsAndExcludeHiddenOrDeadMonsters) {
    auto unit = std::make_unique<CMonstorUnit>();
    mods::StopTrackingEntities();
    unit->CleanViewMonstor(0);
    unit->monster[0].state = 1;
    unit->monster[0].hp = 25;
    unit->monster[0].max_hp = 50;
    unit->monster[0].revealed = -1;
    unit->monster[1].state = 2;
    unit->monster[1].hp = 25;
    unit->monster[1].revealed = 0;
    const auto before = std::make_unique<CMonstorUnit>(*unit);
    auto       list = mods::ReadEntities(unit.get(), "entities.list", nlohmann::json::object());
    ASSERT_EQ(list["entities"].size(), 1);
    const auto arguments = nlohmann::json{
        {"entity", list["entities"][0]["entity"]}
    };
    EXPECT_EQ(mods::ReadEntities(unit.get(), "entities.get", arguments)["hp"], 25);
    EXPECT_EQ(std::memcmp(unit.get(), before.get(), sizeof(CMonstorUnit)), 0);
    unit->monster[0].hp = 0;
    EXPECT_EQ(mods::ReadEntities(unit.get(), "entities.get", arguments)["status"], "expired");
    mods::EntitySlotConstructed(unit.get(), 0);
    unit->monster[0].hp = 25;
    EXPECT_EQ(mods::ReadEntities(unit.get(), "entities.get", arguments)["status"], "expired");
    EXPECT_EQ(mods::ReadEntities(nullptr, "entities.get", arguments)["status"], "expired");
    EXPECT_THROW(mods::ReadEntities(nullptr, "entities.list", nlohmann::json::object()), std::runtime_error);
    for (const auto &id : {"0", "01", "-1", "1x", "18446744073709551616"}) {
        EXPECT_FALSE(mods::ValidateEntityQuery("entities.get", {
                                                                   {"entity", id}
        })
                         .empty());
    }
}

TEST(ModEntityHandles, NativeFloorCleanupExpiresTrackedSlotsOnly) {
    auto first = std::make_unique<CMonstorUnit>();
    auto other = std::make_unique<CMonstorUnit>();
    mods::StopTrackingEntities();
    first->CleanViewMonstor(0);
    other->CleanViewMonstor(0);
    first->monster[2].state = 1;
    auto      &handles = mods::TrackEntities(*first);
    const auto before = handles.Get(2);
    ASSERT_NE(before, 0);
    other->CleanViewMonstor(1);
    EXPECT_EQ(handles.Resolve(before), 2);
    first->CleanViewMonstor(1);
    EXPECT_FALSE(handles.Resolve(before));
    EXPECT_EQ(first->alive_count, 0);
    EXPECT_EQ(first->back_dungeon, 1);
    mods::EntitySlotConstructed(first.get(), 2);
    const auto replacement = handles.Get(2);
    EXPECT_NE(before, replacement);
    EXPECT_EQ(handles.Resolve(replacement), 2);
    mods::TrackEntities(*other);
    EXPECT_FALSE(handles.Resolve(replacement));
    mods::StopTrackingEntities();
}

TEST(ModEntityHandles, NativeFullFloorSpawnFailurePreservesExistingIdentities) {
    auto unit = std::make_unique<CMonstorUnit>();
    mods::StopTrackingEntities();
    unit->CleanViewMonstor(0);
    for (auto &actor : unit->monster) {
        actor.state = 1;
    }
    auto      &handles = mods::TrackEntities(*unit);
    const auto before = handles.Get(0);
    float      position[4]{};
    EXPECT_EQ(unit->SetupViewMonstor(0, position, -1), 0);
    EXPECT_EQ(handles.Get(0), before);
    mods::ExpireEntitiesOutsideUnit(nullptr);
    EXPECT_FALSE(handles.Resolve(before));
    const auto after = mods::TrackEntities(*unit).Get(0);
    EXPECT_NE(after, before);
    mods::StopTrackingEntities();
}

TEST(ModEntityHandles, ReusedSlotsAndFloorClearsExpireEveryOldHandle) {
    mods::EntityHandles handles;
    handles.Activate();
    std::array<mods::EntityHandles::Handle, mods::EntityHandles::Capacity> old{};
    for (unsigned i = 0; i < handles.Capacity; ++i) {
        old[i] = handles.Construct(i);
        ASSERT_EQ(handles.Resolve(old[i]), i);
    }
    const auto replacement = handles.Construct(3);
    EXPECT_NE(replacement, old[3]);
    EXPECT_FALSE(handles.Resolve(old[3]));
    EXPECT_EQ(handles.Resolve(replacement), 3);
    handles.Clear();
    for (unsigned i = 0; i < handles.Capacity; ++i) {
        EXPECT_FALSE(handles.Resolve(old[i]));
        EXPECT_NE(handles.Construct(i), old[i]);
    }
    EXPECT_FALSE(handles.Resolve(replacement));
}

TEST(ModEntityHandles, UnloadAndRetirementDoNotReuseIdentities) {
    mods::EntityHandles handles;
    handles.Activate();
    const auto first = handles.Construct(0);
    handles.Retire(0);
    EXPECT_FALSE(handles.Resolve(first));
    const auto second = handles.Construct(0);
    handles.Deactivate();
    EXPECT_FALSE(handles.Resolve(second));
    handles.Activate();
    const auto third = handles.Construct(0);
    EXPECT_NE(first, third);
    EXPECT_NE(second, third);
    EXPECT_EQ(handles.Construct(16), 0);
    EXPECT_EQ(handles.Get(16), 0);
    EXPECT_FALSE(handles.Resolve(UINT64_MAX));
}
