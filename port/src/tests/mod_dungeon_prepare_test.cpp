#include <gtest/gtest.h>

#include "chronicle/dungeon_prepare.hpp"
#include "chronicle/random_state.hpp"
#include "dun/gameloop.hpp"

TEST(ModDungeonPrepare, InvalidRequestRestoresContextAndEntireRandomState) {
    auto       map = std::make_unique<CDungeonMap>();
    auto       events = std::make_unique<CDungeonEventMan>();
    auto       status = std::make_unique<CDngStatusData>();
    auto      *old_map = NowDngMap;
    auto      *old_events = NowEventMan;
    auto      *old_status = UserStatus;
    const auto random = chronicle::RandomState();
    chronicle::RestoreRandomState(0xfedcba98u);
    EXPECT_THROW(chronicle::dungeon::PrepareMaps(*map, *events, *status, {1, 46, 3, 4}), std::invalid_argument);
    EXPECT_EQ(chronicle::RandomState(), 0xfedcba98u);
    EXPECT_EQ(NowDngMap, old_map);
    EXPECT_EQ(NowEventMan, old_events);
    EXPECT_EQ(UserStatus, old_status);
    EXPECT_FALSE(chronicle::dungeon::CustomBuild());
    chronicle::RestoreRandomState(random);
}

TEST(ModDungeonPrepare, ScratchSnapshotRejectsWrongSizeBeforeMutation) {
    const auto before = CaptureDungeonBuildScratch();
    ASSERT_FALSE(before.empty());
    EXPECT_FALSE(RestoreDungeonBuildScratch(std::span(before).first(before.size() - 1)));
    EXPECT_EQ(CaptureDungeonBuildScratch(), before);
    EXPECT_TRUE(RestoreDungeonBuildScratch(before));
    EXPECT_EQ(CaptureDungeonBuildScratch(), before);
}
