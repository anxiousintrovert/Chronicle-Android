#include <gtest/gtest.h>

#include "mods/entity_events.hpp"

TEST(ModEntityEvents, CaptureQueueIsDormantUntilEnabled) {
    mods::EnemyDeathQueue queue;
    queue.Push({1, 0, 1});
    EXPECT_TRUE(queue.Pending().empty());
    queue.Enable(true);
    queue.Push({1, 0, 1});
    ASSERT_EQ(queue.Pending().size(), 1);
    EXPECT_EQ(queue.Pending()[0].entity, 1);
    queue.Enable(false);
    EXPECT_TRUE(queue.Pending().empty());
    queue.Enable(true);
    EXPECT_TRUE(queue.Pending().empty());
}

TEST(ModEntityEvents, BoundedCapturePreservesOrderAndReportsOverflow) {
    mods::EnemyDeathQueue queue;
    queue.Enable(true);
    for (unsigned i = 0; i < 70; ++i) {
        queue.Push({i + 1, 0, 1});
    }
    ASSERT_EQ(queue.Pending().size(), 64);
    EXPECT_EQ(queue.Dropped(), 6);
    for (unsigned i = 0; i < 64; ++i) {
        EXPECT_EQ(queue.Pending()[i].entity, i + 1);
    }
    queue.Reset();
    EXPECT_TRUE(queue.Pending().empty());
    EXPECT_EQ(queue.Dropped(), 0);
    EXPECT_TRUE(queue.Enabled());
}
