#include <gtest/gtest.h>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include "collisiondata.hpp"
#include "chronicle/monster_hits.hpp"

TEST(ModMonsterHits, RetailPreparationIsDormantAndQueriesIgnoreExtraStorage) {
    auto data = std::make_unique<CCollisionData>();
    std::fill(std::begin(data->active), std::end(data->active), 0);
    data->active[96] = 1;
    data->hit[96].target_mask = 3;
    data->hit[96].phase = data->hit[96].ready_phase = 0;
    data->hit[96].radius = 10;
    float position[4] = {0, 0, 0, 1};
    sceVu0CopyVector(data->hit[96].pos, position);
    data->PrepareForMonsterSlots(16);
    EXPECT_EQ(data->HitLimit(), 96u);
    EXPECT_EQ(data->active[96], 1);
    EXPECT_EQ(data->FindMonsterHit(position, 10), -1);
    EXPECT_EQ(data->CheckHitUser(position, 1, 10), -1);
    EXPECT_THROW(data->PrepareForMonsterSlots(257), std::invalid_argument);
    EXPECT_EQ(data->active[96], 1);
}

TEST(ModMonsterHits, FullExpandedPoolPreservesOwnersAndFailsWithoutOverwriting) {
    auto data = std::make_unique<CCollisionData>();
    data->PrepareForMonsterSlots(256);
    ASSERT_EQ(data->HitLimit(), chronicle::monsters::HitStorageCapacity);
    float position[4] = {0, 0, 0, 1};
    for (unsigned i = 0; i < data->HitLimit(); ++i) {
        ASSERT_EQ(data->Set(position, int(i), 2, 10, 0, 3, 0, 0, 0), int(i));
        data->SetUserID(200 + 255 * 5, int(i % 16));
    }
    EXPECT_THROW(data->Set(position, 99999, 2, 10, 0, 3, 0, 0, 0), std::runtime_error);
    EXPECT_EQ(data->hit[0].damage, 0);
    const auto last = data->HitLimit() - 1;
    EXPECT_EQ(data->hit[last].damage, int(last));
    EXPECT_EQ(data->hit[last].owner, 1475);
    std::fill_n(data->active, last, 0);
    EXPECT_EQ(data->FindMonsterHit(position, 10), int(last));
    EXPECT_EQ(data->CheckHitUser(position, 1, 10), int(last));
    data->PrepareForMonsterSlots(16);
    EXPECT_EQ(data->HitLimit(), 96u);
    EXPECT_EQ(data->now_hit, 0);
    EXPECT_EQ(data->active[last], 0);
    EXPECT_EQ(data->Set(position, 7, 2, 10, 0, 3, 0, 0, 0), 0);
}
