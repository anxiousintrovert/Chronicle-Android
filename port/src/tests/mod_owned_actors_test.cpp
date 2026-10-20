#include <gtest/gtest.h>
#include "mods/owned_actors.hpp"

TEST(ModOwnedActors, OwnershipCannotCrossSlotReuseOrAnotherOwner) {
    mods::EntityHandles handles; handles.Activate();
    mods::ActorOwners owners;
    std::string error;
    const auto first = handles.Construct(3);
    ASSERT_TRUE(owners.Claim(handles, first, "alpha", "alpha:guard", error));
    EXPECT_TRUE(owners.CheckOwner(handles, first, "alpha", error));
    EXPECT_FALSE(owners.CheckOwner(handles, first, "beta", error));
    EXPECT_NE(error.find("alpha"), std::string::npos);
    EXPECT_FALSE(owners.Claim(handles, first, "beta", "beta:guard", error));
    const auto second = handles.Construct(3);
    EXPECT_FALSE(owners.Find(handles, first));
    EXPECT_FALSE(owners.Find(handles, second));
    EXPECT_FALSE(owners.CheckOwner(handles, second, "alpha", error));
    EXPECT_TRUE(owners.ByOwner(handles, "alpha").empty());
    ASSERT_TRUE(owners.Claim(handles, second, "beta", "beta:guard", error));
    EXPECT_EQ(owners.Size(), 1);
    EXPECT_EQ(owners.Find(handles, second)->definition, "beta:guard");
    EXPECT_FALSE(owners.CheckOwner(handles, second, "alpha", error));
    EXPECT_TRUE(owners.CheckOwner(handles, second, "beta", error));
}
TEST(ModOwnedActors, RetirementEnumerationDoesNotLoseLiveCleanupRecords) {
    mods::EntityHandles handles; handles.Activate();
    mods::ActorOwners owners; std::string error;
    const auto a = handles.Construct(0), b = handles.Construct(1), retail = handles.Construct(2);
    ASSERT_TRUE(owners.Claim(handles, a, "alpha", "guard", error));
    ASSERT_TRUE(owners.Claim(handles, b, "beta", "guard", error));
    EXPECT_FALSE(owners.CheckOwner(handles, retail, "alpha", error));
    EXPECT_EQ(owners.ByOwner(handles, "alpha"), (std::vector<mods::EntityHandles::Handle>{a}));
    EXPECT_EQ(owners.Size(), 2);
    EXPECT_TRUE(owners.Find(handles, a)); // enumerating unload work is not despawn
    owners.Forget(a);
    EXPECT_TRUE(handles.Resolve(a)); // metadata deletion does not touch the engine identity
    EXPECT_TRUE(owners.Find(handles, b));
    handles.Deactivate(); owners.Expire(handles);
    EXPECT_EQ(owners.Size(), 0);
    EXPECT_FALSE(owners.Claim(handles, b, "beta", "guard", error));
}
TEST(ModOwnedActors, RejectsInvalidClaimsAndHandlesEverySupportedSlot) {
    mods::EntityHandles handles; handles.Activate();
    mods::ActorOwners owners; std::string error;
    const auto first = handles.Construct(0);
    EXPECT_FALSE(owners.Claim(handles, first, "", "guard", error));
    EXPECT_FALSE(owners.Claim(handles, first, "alpha", "", error));
    EXPECT_FALSE(owners.Claim(handles, first, std::string(129, 'x'), "guard", error));
    EXPECT_EQ(owners.Size(), 0);
    for (unsigned slot = 0; slot < mods::EntityHandles::Capacity; ++slot)
        ASSERT_TRUE(owners.Claim(handles, handles.Construct(slot), "alpha", "guard", error));
    EXPECT_EQ(owners.Size(), mods::EntityHandles::Capacity);
    EXPECT_EQ(owners.ByOwner(handles, "alpha").size(), mods::EntityHandles::Capacity);
    handles.Clear(); owners.Expire(handles);
    EXPECT_EQ(owners.Size(), 0);
}

TEST(ModOwnedActors, SeparateIdentityRegistriesCannotAliasOwnership) {
    mods::EntityHandles first, second; first.Activate(); second.Activate();
    const auto a = first.Construct(0), b = second.Construct(0);
    ASSERT_EQ(a, b); // generations are local to a registry
    mods::ActorOwners owners; std::string error;
    ASSERT_TRUE(owners.Claim(first, a, "alpha", "guard", error));
    EXPECT_FALSE(owners.Find(second, b));
    EXPECT_FALSE(owners.Claim(second, b, "alpha", "guard", error));
    owners.Expire(second);
    EXPECT_TRUE(owners.Find(first, a));
}
