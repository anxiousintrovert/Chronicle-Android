#include <gtest/gtest.h>

#include <cstring>
#include <memory>

#include "mods/api.hpp"
#include "savedata.hpp"

namespace {
TEST(ModFloorRules, SavedRulesDoNotChangeActiveRuleAndMixedFailureRollsBack) {
    auto save = std::make_unique<CSaveData>();
    save->GetDngStatus()->res_limit_zone_current = 10;
    auto              before = std::make_unique<CSaveData>(*save);
    mods::GameCommand rule{
        "dungeon.floor_rules_set", {{"dungeon", 0}, {"floor", 1}, {"rule", 11}},
         "test.rules"
    };
    std::string error;
    ASSERT_TRUE(mods::ApplySave(*save, {rule}, 0, error)) << error;
    before->GetDngStatus()->res_limit_zone_id[0][1] = 11;
    EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    auto snapshot = mods::ReadSave(*save, {
    },
                                   "dungeon.floor_rules_get", {{"dungeon", 0}, {"floor", 1}});
    EXPECT_EQ(snapshot["rule"], 11);
    EXPECT_EQ(snapshot["active_rule"], 10);
    rule.arguments["rule"] = -1;
    EXPECT_FALSE(mods::ApplySave(*save, {
                                            rule, {"economy.spend", {{"amount", 65535}}, "test.rules"}
    },
                                 0, error));
    EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
}

TEST(ModFloorRules, ActiveApplicationRequiresMatchingContextAndCharacter) {
    auto  save = std::make_unique<CSaveData>();
    auto &s = *save->GetDngStatus();
    s.party_size = 2;
    s.cur_chara = 0;
    s.cur_georama = 0;
    s.cur_floor = 1;
    mods::GameCommand rule{
        "dungeon.floor_rules_set", {{"dungeon", 0}, {"floor", 1}, {"rule", 11}, {"apply_current", true}},
         "test.rules"
    };
    std::string error;
    EXPECT_FALSE(mods::ApplySave(*save, {rule}, 0, error));
    auto apply = [&] { return mods::ApplySave(*save, {rule}, 0, error, nullptr, nullptr, nullptr, false, true); };
    ASSERT_TRUE(apply()) << error;
    EXPECT_EQ(s.res_limit_zone_current, 11);
    rule.arguments["floor"] = 2;
    EXPECT_FALSE(apply());
    rule.arguments["floor"] = 1;
    rule.arguments["rule"] = 1;
    EXPECT_FALSE(apply());
    rule.arguments["rule"] = 0;
    ASSERT_TRUE(apply()) << error;
    rule.arguments["rule"] = -1;
    ASSERT_TRUE(apply()) << error;
    EXPECT_EQ(s.res_limit_zone_current, -1);
    EXPECT_EQ(s.res_limit_zone_id[0][1], -1);
}

TEST(ModFloorRules, SchemaUsesActualRetailBoundsAndDefinedRules) {
    for (int rule : {-2, 6, 7, 8, 9, 12}) {
        EXPECT_FALSE(mods::ValidateOperation("dungeon.floor_rules_set", {
                                                                            {"dungeon", 0   },
                                                                            {"floor",   0   },
                                                                            {"rule",    rule}
        })
                         .empty());
    }
    for (int rule : {-1, 0, 1, 2, 3, 4, 5, 10, 11}) {
        EXPECT_TRUE(mods::ValidateOperation("dungeon.floor_rules_set", {
                                                                           {"dungeon", 0   },
                                                                           {"floor",   0   },
                                                                           {"rule",    rule}
        })
                        .empty());
    }
    EXPECT_FALSE(mods::ValidateOperation("dungeon.floor_rules_get", {
                                                                        {"dungeon", 0 },
                                                                        {"floor",   15}
    })
                     .empty());
    EXPECT_FALSE(mods::ValidateOperation("dungeon.floor_rules_get", {
                                                                        {"dungeon", 6},
                                                                        {"floor",   0}
    })
                     .empty());
    EXPECT_FALSE(mods::ValidateOperation("dungeon.floor_rules_get", {
                                                                        {"dungeon", 0  },
                                                                        {"floor",   0.0}
    })
                     .empty());
}
} // namespace
