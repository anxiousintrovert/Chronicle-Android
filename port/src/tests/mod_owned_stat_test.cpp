#include <gtest/gtest.h>

#include "mods/owned_stat.hpp"

TEST(ModOwnedStat, ExclusiveClaimsReportTheConflictingOwnerWithoutChangingItsBaseline) {
    mods::OwnedStat stat;
    std::string     error;
    EXPECT_FALSE(stat.Claim("", 12, 30, error));
    ASSERT_TRUE(stat.Claim("first.mod", 12, 30, error));
    EXPECT_FALSE(stat.Claim("second.mod", 30, 40, error));
    EXPECT_NE(error.find("first.mod"), std::string::npos);
    EXPECT_EQ(stat.owner, "first.mod");
    EXPECT_EQ(stat.original, 12);
    EXPECT_EQ(stat.applied, 30);
    EXPECT_EQ(stat.Release("second.mod", 30), 30);
    EXPECT_EQ(stat.Release("first.mod", 30), 12);
    EXPECT_TRUE(stat.owner.empty());
}

TEST(ModOwnedStat, RepeatedRequestsKeepBaselineButReleasePreservesLaterNativeChanges) {
    mods::OwnedStat stat;
    std::string     error;
    ASSERT_TRUE(stat.Claim("mod", 12, 30, error));
    ASSERT_TRUE(stat.Claim("mod", 30, 40, error));
    EXPECT_EQ(stat.original, 12);
    EXPECT_EQ(stat.Release("mod", 50), 50);
    ASSERT_TRUE(stat.Claim("mod", 12, 30, error));
    ASSERT_TRUE(stat.Claim("mod", 50, 60, error));
    EXPECT_EQ(stat.Release("mod", 60), 50);
}

TEST(ModOwnedStat, FailedStagedChangesDoNotAcquireLiveOwnership) {
    mods::OwnedStat live;
    auto            staged = live;
    std::string     error;
    ASSERT_TRUE(staged.Claim("mod", 1, 2, error));
    EXPECT_TRUE(live.owner.empty());
    live = staged;
    EXPECT_EQ(live.Release("mod", 2), 1);
}
