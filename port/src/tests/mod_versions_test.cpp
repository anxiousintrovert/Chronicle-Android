#include <gtest/gtest.h>

#include "mods/versions.hpp"

namespace {
TEST(ModVersions, SemanticPrecedenceAndBuildMetadata) {
    const char *ordered[] = {"1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0", "1.0.1", "1.2.0", "1.10.0", "2.0.0"};
    for (unsigned i = 1; i < std::size(ordered); ++i) {
        ASSERT_EQ(mods::CompareVersions(ordered[i - 1], ordered[i]), -1);
        ASSERT_EQ(mods::CompareVersions(ordered[i], ordered[i - 1]), 1);
    }
    EXPECT_EQ(mods::CompareVersions("1.0.0+one", "1.0.0+two"), 0);
    EXPECT_EQ(mods::CompareVersions("999999999999999999999.0.0", "1000000000000000000000.0.0"), -1);
}

TEST(ModVersions, InvalidRangesFailClosedButExactLegacyVersionsRemainSupported) {
    for (auto bad : {"1", "01.2.3", "1.2.3-01", "1.2.3-", "1.2.3+", "1.2.3-alpha..beta", "1.2.3+one+two", "1.2.3.4", "1.2.3 "}) {
        EXPECT_FALSE(mods::CompareVersions(bad, "1.0.0").has_value()) << bad;
    }
    mods::VersionRequirement range{"", "1.2.0", "2.0.0"};
    EXPECT_TRUE(range.Validate().empty());
    EXPECT_TRUE(range.Accepts("1.2.0+build"));
    EXPECT_TRUE(range.Accepts("1.9.0-beta"));
    EXPECT_FALSE(range.Accepts("1.2.0-rc.1"));
    EXPECT_FALSE(range.Accepts("2.0.0"));
    EXPECT_FALSE(range.Accepts("1"));
    EXPECT_FALSE((mods::VersionRequirement{"", "2.0.0", "1.0.0"}).Validate().empty());
    EXPECT_FALSE((mods::VersionRequirement{"1", "1.0.0", ""}).Validate().empty());
    EXPECT_TRUE((mods::VersionRequirement{"legacy-v1", "", ""}).Accepts("legacy-v1"));
    EXPECT_FALSE((mods::VersionRequirement{"1.0.0+one", "", ""}).Accepts("1.0.0+two"));
}
} // namespace
