#include <gtest/gtest.h>

#include <array>

#include "draw3d.hpp"

TEST(Draw3DVisualRetirement, RemovesOnlyOwnedRecordsWithoutReadingTheirStorage) {
    std::array<unsigned int, 48> blocks{};
    auto                        *first = blocks.data();
    auto                        *second = first + 16;
    auto                        *outside = first + 32;
    Draw3DRegisterVisual(first, false);
    Draw3DRegisterVisual(second, true);
    Draw3DRegisterVisual(outside, false);
    Draw3DRetireVisuals(nullptr, sizeof(blocks));
    Draw3DRetireVisuals(first, 0);
    ASSERT_NE(Draw3DFindVisual(first), nullptr);
    Draw3DRetireVisuals(first, 32 * sizeof(unsigned int));
    EXPECT_EQ(Draw3DFindVisual(first), nullptr);
    EXPECT_EQ(Draw3DFindVisual(second), nullptr);
    EXPECT_NE(Draw3DFindVisual(outside), nullptr);
    Draw3DRetireVisuals(outside, 16 * sizeof(unsigned int));
    EXPECT_EQ(Draw3DFindVisual(outside), nullptr);
}
