#include <gtest/gtest.h>
#include "../mods/retail_sound.hpp"
#include <cstring>

TEST(ModRetailSound, RejectsTableTailsUnusedRowsAndInvalidSelectors) {
    std::string error;
    EXPECT_FALSE(mods::ResolveRetailEffect(-1, -1, -1, -1, error));
    EXPECT_FALSE(mods::ResolveRetailEffect(2800, -1, -1, -1, error));
    EXPECT_FALSE(mods::ResolveRetailEffect(299, 0, -1, -1, error));
    EXPECT_FALSE(mods::ResolveRetailEffect(299, 1, -1, -1, error));
    EXPECT_FALSE(mods::ResolveRetailEffect(344, -1, 3, -1, error));
    EXPECT_FALSE(mods::ResolveRetailEffect(343, -1, 3, -1, error)); // terminator
    EXPECT_FALSE(mods::ResolveRetailEffect(407, -1, -1, 6, error));
    EXPECT_FALSE(mods::ResolveRetailEffect(406, -1, -1, 6, error)); // terminator
    EXPECT_FALSE(mods::ResolveRetailEffect(100, 2, -1, -1, error));
    EXPECT_FALSE(mods::ResolveRetailEffect(300, -1, 101, -1, error));
    EXPECT_FALSE(mods::ResolveRetailEffect(400, -1, -1, 11, error));
    EXPECT_FALSE(mods::ResolveRetailEffect(404, -1, -1, 0, error)); // unused voice entry
}
TEST(ModRetailSound, ReturnsDetachedNativeRowsWithoutChangingTablesOrRandomStream) {
    std::string error;
    const auto before = voice_info[0][0];
    srand(981); const auto expected = rand(); srand(981);
    auto info = mods::ResolveRetailEffect(400, -1, -1, 0, error);
    ASSERT_TRUE(info) << error;
    EXPECT_EQ(info->bank, 23);
    EXPECT_EQ(info->prog, 20);
    info->bank = 1;
    EXPECT_EQ(std::memcmp(&before, &voice_info[0][0], sizeof(before)), 0);
    EXPECT_EQ(rand(), expected);
    // Absent chapter tables follow native lookup's fixed-table fallback.
    const auto fallback = mods::ResolveRetailEffect(300, -1, 5, -1, error);
    if (se_info[300].bank >= 0 && se_info[300].prog >= 0) {
        ASSERT_TRUE(fallback);
        EXPECT_EQ(fallback->bank, se_info[300].bank);
        EXPECT_EQ(fallback->prog, se_info[300].prog);
    } else EXPECT_FALSE(fallback);
}
