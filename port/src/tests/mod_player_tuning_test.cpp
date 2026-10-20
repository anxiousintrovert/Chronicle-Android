#include <gtest/gtest.h>

#include <cstdlib>
#include <limits>

#include "mods/player_tuning.hpp"

namespace {
using Tuning = mods::PlayerTuning;
using Json = nlohmann::json;

mods::GameCommand Set(std::string owner, Json args) { return {"player.tuning_set", std::move(args), std::move(owner)}; }

mods::GameCommand Clear(std::string owner) { return {"player.tuning_clear", Json::object(), std::move(owner)}; }

class ModPlayerTuning : public testing::Test {
protected:
    Tuning      tuning;
    std::string error;

    bool Apply(std::vector<mods::GameCommand> commands, std::uint64_t epoch = 7) {
        return tuning.Stage(commands, epoch, error);
    }
};

TEST_F(ModPlayerTuning, DormantIdentityDoesNotConsumeRngOrChangeOnUnrelatedTransactions) {
    const auto before = tuning.State();
    std::srand(723);
    const int next = std::rand();
    std::srand(723);
    ASSERT_TRUE(Apply({
        {"economy.money_add", {{"amount", 1}}, "wallet"}
    }));
    tuning.Retain({}, 10);
    EXPECT_EQ(before, tuning.State());
    EXPECT_EQ(std::rand(), next);
    EXPECT_FALSE(tuning.Engaged());
    EXPECT_EQ(tuning.Value(Tuning::RunSpeed), 1);
    EXPECT_EQ(tuning.Value(Tuning::AttackSpeed), 1);
    EXPECT_EQ(tuning.Value(Tuning::Recoil), 1);
    EXPECT_EQ(tuning.Value(Tuning::GaugeRefill), 1);
    EXPECT_EQ(tuning.Value(Tuning::ThirstDrain), 1);
    EXPECT_EQ(tuning.Value(Tuning::DurabilityWear), 1);
    EXPECT_EQ(tuning.Value(Tuning::InvincibleBonus), 0);
    EXPECT_EQ(tuning.Value(Tuning::MapReveal), 0);
    EXPECT_EQ(tuning.Value(Tuning::MonsterReveal), 0);
}

TEST_F(ModPlayerTuning, AcceptsExactBoundedSchemaAndRejectsNonfiniteAndSpoofedFields) {
    for (const char *field : {"run_speed", "attack_anim_speed", "recoil_scale", "gauge_refill", "thirst_drain", "durability_wear"}) {
        for (double value : {0.25, 1.0, 4.0}) {
            EXPECT_TRUE(Tuning::Validate("player.tuning_set", {
                                                                  {field, value}
            })
                            .empty());
        }
        for (double value : {0.0, 0.249, 4.001, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
            EXPECT_FALSE(Tuning::Validate("player.tuning_set", {
                                                                   {field, value}
            })
                             .empty());
        }
        EXPECT_FALSE(Tuning::Validate("player.tuning_set", {
                                                               {field, true}
        })
                         .empty());
    }
    for (int value : {0, 120}) {
        EXPECT_TRUE(Tuning::Validate("player.tuning_set", {
                                                              {"invincible_bonus_frames", value}
        })
                        .empty());
    }
    for (Json value : {Json(-1), Json(121), Json(1.0), Json(true), Json("1")}) {
        EXPECT_FALSE(Tuning::Validate("player.tuning_set", {
                                                               {"invincible_bonus_frames", value}
        })
                         .empty());
    }
    for (const char *field : {"map_reveal", "monster_reveal"}) {
        EXPECT_TRUE(Tuning::Validate("player.tuning_set", {
                                                              {field, true}
        })
                        .empty());
        EXPECT_FALSE(Tuning::Validate("player.tuning_set", {
                                                               {field, 1}
        })
                         .empty());
    }
    EXPECT_FALSE(Tuning::Validate("player.tuning_set", {
                                                           {"owner", "other"}
    })
                     .empty());
    EXPECT_FALSE(Tuning::Validate("player.tuning_clear", {
                                                             {"run_speed", 1}
    })
                     .empty());
    EXPECT_FALSE(Tuning::Validate("unknown", Json::object()).empty());
    EXPECT_FALSE(Tuning::Validate("player.tuning_set", Json::array()).empty());
    EXPECT_FALSE(Apply({
        Set("", {{"run_speed", 2}}
         )
    }));
}

TEST_F(ModPlayerTuning, DisjointOwnersCoexistAndConflictsIdentifyFieldAndOwner) {
    ASSERT_TRUE(Apply({
        Set("movement", {{"run_speed", 2}     }
         ), Set("survival", {{"thirst_drain", 0.5}}
         )
    }));
    const auto before = tuning.State();
    EXPECT_FALSE(Apply({
        Set("intruder", {{"run_speed", 3}}
         )
    }));
    EXPECT_NE(error.find("run_speed"), std::string::npos);
    EXPECT_NE(error.find("movement"), std::string::npos);
    EXPECT_EQ(before, tuning.State());
    ASSERT_TRUE(Apply({Clear("intruder")}));
    EXPECT_EQ(before, tuning.State());
}

TEST_F(ModPlayerTuning, ReplacingRequestReleasesOmittedFieldsAndExplicitDefaultsStillClaim) {
    ASSERT_TRUE(Apply({
        Set("one", {{"run_speed", 2}, {"map_reveal", true}}
         )
    }));
    ASSERT_TRUE(Apply({
        Set("one", {{"gauge_refill", 1}, {"monster_reveal", false}}
         )
    }));
    EXPECT_EQ(tuning.Value(Tuning::RunSpeed), 1);
    EXPECT_EQ(tuning.Value(Tuning::MapReveal), 0);
    EXPECT_FALSE(Apply({
        Set("two", {{"gauge_refill", 2}}
         )
    }));
    EXPECT_FALSE(Apply({
        Set("two", {{"monster_reveal", true}}
         )
    }));
    ASSERT_TRUE(Apply({
        Set("one", Json::object()), Set("two", {{"gauge_refill", 2}}
           )
    }));
    EXPECT_EQ(tuning.State()["owners"]["gauge_refill"], "two");
}

TEST_F(ModPlayerTuning, FailedMultiOwnerBatchAndDetachedTransactionsCannotLeak) {
    ASSERT_TRUE(Apply({
        Set("one", {{"run_speed", 2}}
         )
    }));
    const auto before = tuning.State();
    EXPECT_FALSE(Apply({
        Clear("one"), Set("two", {{"map_reveal", true} }
          ), Set("three", {{"map_reveal", false}}
          )
    }));
    EXPECT_EQ(before, tuning.State());
    Tuning           staged = tuning;
    const std::array commands{
        Set("one", {{"run_speed", 3}}
         )
    };
    ASSERT_TRUE(staged.Stage(commands, 7, error));
    EXPECT_EQ(before, tuning.State());
    tuning.Swap(staged);
    EXPECT_EQ(tuning.Value(Tuning::RunSpeed), 3);
    EXPECT_EQ(staged.State(), before);
}

TEST_F(ModPlayerTuning, OwnerShutdownAndRuntimeEpochResetClaimsWithoutSavedStateWrites) {
    ASSERT_TRUE(Apply({
        Set("one", {{"run_speed", 2}                }
         ), Set("two", {{"invincible_bonus_frames", 120}}
         )
    }));
    const std::array<std::string, 1> remaining{"two"};
    tuning.Retain(remaining, 7);
    EXPECT_EQ(tuning.Value(Tuning::RunSpeed), 1);
    EXPECT_EQ(tuning.Value(Tuning::InvincibleBonus), 120);
    tuning.Retain(remaining, 8);
    EXPECT_FALSE(tuning.Engaged());
    EXPECT_EQ(tuning.Value(Tuning::InvincibleBonus), 0);
    ASSERT_TRUE(Apply({
                          Set("one", {{"run_speed", 2}}
                           )
    },
                      8));
    const auto before = tuning.State();
    EXPECT_FALSE(Apply({
                           Set("two", {{"run_speed", 99}}
                            )
    },
                       9));
    EXPECT_EQ(tuning.State(), before);
    ASSERT_TRUE(Apply({
                          Set("two", {{"run_speed", 3}}
                           )
    },
                      9));
    EXPECT_EQ(tuning.State()["owners"]["run_speed"], "two");
    EXPECT_EQ(tuning.State()["epoch"], 9);
}
} // namespace
