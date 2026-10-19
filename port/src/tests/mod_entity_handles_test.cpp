#include <gtest/gtest.h>

#include "mods/entities.hpp"
#include "mods/entity_handles.hpp"
#include "mods/entity_tracking.hpp"
#include "monstorunit.hpp"

TEST(ModEntityHandles, DormantRegistryCreatesNoIdentities) {
    mods::EntityHandles handles;
    for (unsigned i = 0; i < handles.Capacity; ++i) {
        EXPECT_EQ(handles.Construct(i), 0);
        EXPECT_EQ(handles.Get(i), 0);
    }
    EXPECT_FALSE(handles.Resolve(0));
    EXPECT_FALSE(handles.Resolve(16));
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
