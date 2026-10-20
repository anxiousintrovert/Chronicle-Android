#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "chronicle/dungeon_transport.hpp"

TEST(ModDungeonTransport, RetailBypassesInspectionAndExpandedRejectsUnknownPrograms) {
    const std::vector<char> invalid{'n', 'o', 't', 'a', 's', 'c', 'r', 'i', 'p', 't'};
    EXPECT_TRUE(ExpandedTransportScript(invalid, -1, 20, 20).empty());
    EXPECT_THROW(ExpandedTransportScript(invalid, 0, 48, 48), std::runtime_error);
    EXPECT_THROW(ExpandedTransportScript(invalid, 0, 20, 48), std::runtime_error);
    EXPECT_THROW(ExpandedTransportScript(invalid, 7, 48, 48), std::runtime_error);
}

TEST(ModDungeonTransport, VerifiedRetailProgramsRelocateOnlyFloatOperandsAndPreserveInputs) {
    const char *root = std::getenv("DC_DATA");
    if (!root) {
        GTEST_SKIP() << "Requires local PAL data";
    }
    constexpr int expected_changes[]{7, 6, 6, 7, 0, 5, 0};
    for (int dungeon = 0; dungeon < 7; ++dungeon) {
        SCOPED_TRACE(dungeon);
        std::ifstream stream(std::filesystem::path(root) / ("dun/script/d0" + std::to_string(dungeon + 1) + "/event.stb"), std::ios::binary);
        ASSERT_TRUE(stream.good());
        std::vector<char> original((std::istreambuf_iterator<char>(stream)), {});
        const auto        before = original;
        auto              result = ExpandedTransportScript(original, dungeon, 48, 48);
        EXPECT_EQ(original, before);
        if (!expected_changes[dungeon]) {
            EXPECT_TRUE(result.empty());
            continue;
        }
        ASSERT_EQ(result.size(), original.size());
        int changes = 0;
        for (std::size_t at = 0; at + 4 <= result.size(); at += 4) {
            if (!std::memcmp(result.data() + at, original.data() + at, 4)) {
                continue;
            }
            ASSERT_GE(at, 8u);
            int   op, type;
            float old_value, new_value;
            std::memcpy(&op, original.data() + at - 8, 4);
            std::memcpy(&type, original.data() + at - 4, 4);
            std::memcpy(&old_value, original.data() + at, 4);
            std::memcpy(&new_value, result.data() + at, 4);
            EXPECT_EQ(op, 3);
            EXPECT_EQ(type, 2);
            EXPECT_FLOAT_EQ(new_value - old_value, 4480.0f);
            ++changes;
        }
        EXPECT_EQ(changes, expected_changes[dungeon]);
        original.back() ^= 1;
        EXPECT_THROW(ExpandedTransportScript(original, dungeon, 48, 48), std::runtime_error);
    }
}
