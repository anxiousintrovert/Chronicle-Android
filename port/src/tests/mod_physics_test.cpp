#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <limits>

#include "mods/api.hpp"
#include "mods/entities.hpp"
#include "mods/entity_tracking.hpp"
#include "mods/runtime.hpp"
#include "monstorunit.hpp"
#include "savedata.hpp"

namespace {
using Json = nlohmann::json;

class ModPhysics : public testing::Test {
protected:
    std::unique_ptr<CMonstorUnit> unit;
    std::unique_ptr<CSaveData>    save;
    std::string                   id, error;

    void SetUp() override {
        mods::StopTrackingEntities();
        unit = std::make_unique<CMonstorUnit>();
        save = std::make_unique<CSaveData>();
        unit->CleanViewMonstor(0);
        auto &a = unit->monster[0];
        a.state = 2;
        a.revealed = 1;
        a.hp = a.max_hp = 50;
        a.ground_distance = 0;
        a.falls = 0;
        id = std::to_string(mods::TrackEntities(*unit).Get(0));
    }

    void TearDown() override { mods::StopTrackingEntities(); }

    mods::GameCommand Set(Json velocity, std::string owner = "test.mod") {
        return {
            "physics.velocity_set", {{"entity", id}, {"velocity", velocity}},
             owner
        };
    }

    Json Read() { return mods::ReadEntities(unit.get(), "physics.velocity_get", {
                                                                                    {"entity", id}
 }); }

    bool Apply(std::vector<mods::GameCommand> commands) {
        return mods::ApplyEntityAndSave(*save, unit.get(), commands, 0, error, nullptr, nullptr);
    }

    void Expect(float x, float y, float z) {
        const auto v = Read().at("velocity");
        EXPECT_NEAR(v[0].get<float>(), x, 0.0001f);
        EXPECT_NEAR(v[1].get<float>(), y, 0.0001f);
        EXPECT_NEAR(v[2].get<float>(), z, 0.0001f);
    }
};
} // namespace

TEST_F(ModPhysics, NativeIntentAccountsForKnockbackFallingAndStop) {
    auto &a = unit->monster[0];
    a.movement[0] = 1;
    a.movement[1] = a.movement[2] = 0;
    a.movement_speed = 3;
    Expect(3, 0, 0);
    a.knockback[0] = 4;
    a.knockback_direction[0] = 0;
    a.knockback_direction[1] = 0;
    a.knockback_direction[2] = -1;
    a.falls = 1;
    a.ground_distance = 10;
    Expect(0, -0.2f, -4);
    EXPECT_EQ(Read()["source"], "knockback");
    a.stop_timer = 1;
    Expect(0, 0, 0);
    EXPECT_EQ(Read()["source"], "stopped");
}

TEST_F(ModPhysics, SetCompensatesGravityAndReplacesKnockbackWithoutTeleporting) {
    auto &a = unit->monster[0];
    float position[4] = {10, 20, 30, 1};
    unit->chara[0][0].SetPosition(position);
    a.knockback[0] = 5;
    a.falls = 1;
    a.ground_distance = 10;
    ASSERT_TRUE(Apply({Set({3, 4, -5})})) << error;
    Expect(3, 4, -5);
    EXPECT_FLOAT_EQ(a.knockback[0], 0);
    unit->chara[0][0].GetPosition(position);
    EXPECT_FLOAT_EQ(position[0], 10);
    EXPECT_FLOAT_EQ(position[1], 20);
    EXPECT_FLOAT_EQ(position[2], 30);
    EXPECT_EQ(a.hp, 50);
    ASSERT_TRUE(Apply({Set({0, 0, 0})})) << error;
    Expect(0, 0, 0);
    a.falls = 0;
    ASSERT_TRUE(Apply({Set({0, 0, 0})}));
    EXPECT_FLOAT_EQ(a.movement_speed, 0);
    EXPECT_FLOAT_EQ(a.movement[3], 1);
}

TEST_F(ModPhysics, FailedMixedSaveOrEntityBatchPublishesNoVelocity) {
    ASSERT_TRUE(Apply({Set({2, 0, 0})}));
    EXPECT_FALSE(Apply({
        Set({8,               0,               0         }
        ), {"economy.spend", {{"amount", 1}}, "test.mod"}
    }));
    Expect(2, 0, 0);
    auto stale = Set({7, 0, 0});
    stale.arguments["entity"] = "999999999";
    EXPECT_FALSE(Apply({Set({9, 0, 0}), stale}));
    Expect(2, 0, 0);
    EXPECT_FALSE(Apply({
        {"combat.status_add", {{"entity", id}, {"status", "stop"}, {"duration_frames", 10}}, "test.mod"},
        Set({9,                   0,                                                             0         }
        )
    }));
    EXPECT_EQ(unit->monster[0].stop_timer, 0);
    Expect(2, 0, 0);
    EXPECT_FALSE(Apply({
        {"combat.damage", {{"entity", id}, {"amount", 50}}, "test.mod"},
        Set({9,               0,                                0         }
        )
    }));
    EXPECT_EQ(unit->monster[0].hp, 50);
    Expect(2, 0, 0);
}

TEST_F(ModPhysics, InputsAndNativeStatesAreValidated) {
    ASSERT_TRUE(Apply({Set({1, 0, 0})}));
    for (auto v : {
             Json{0, 0},
             Json{0, 0, 0, 0},
             Json{100001, 0, 0},
             Json{"1", 0, 0},
             Json{std::numeric_limits<double>::infinity(), 0, 0}
    }) {
        EXPECT_FALSE(Apply({Set(v)}));
        Expect(1, 0, 0);
    }
    EXPECT_FALSE(Apply({Set({0, 0, 0}, "")}));
    unit->monster[0].stop_timer = 2;
    EXPECT_FALSE(Apply({Set({0, 0, 0})}));
    unit->monster[0].stop_timer = 0;
    unit->monster[0].state = 1;
    EXPECT_FALSE(Apply({Set({0, 0, 0})}));
    EXPECT_THROW(Read(), std::runtime_error);
    unit->monster[0].state = 2;
    unit->monster[0].movement_speed = std::numeric_limits<float>::infinity();
    EXPECT_THROW(Read(), std::runtime_error);
    unit->CleanViewMonstor(0);
    EXPECT_THROW(Read(), std::runtime_error);
    EXPECT_FALSE(Apply({Set({0, 0, 0})}));
    EXPECT_THROW(mods::ReadEntities(nullptr, "physics.velocity_get", {
                                                                         {"entity", id}
    }),
                 std::runtime_error);
}

TEST_F(ModPhysics, SphereIncludesBoundaryAndExcludesHiddenInactiveAndNonfiniteOrigins) {
    float position[4] = {3, 0, 4, 1};
    unit->chara[0][0].SetPosition(position);
    auto query = [&](Json args) { return mods::ReadEntities(unit.get(), "physics.query", args); };
    Json args = {
        {"center", {0, 0, 0}},
        {"radius", 5        }
    };
    auto result = query(args)["entities"];
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0]["entity"], id);
    EXPECT_EQ(result[0]["distance"], 5);
    args["radius"] = 4.99;
    EXPECT_TRUE(query(args)["entities"].empty());
    args["center"] = {3, 0, 4};
    args["radius"] = 0;
    EXPECT_EQ(query(args)["entities"].size(), 1);
    unit->monster[0].revealed = 0;
    EXPECT_TRUE(query(args)["entities"].empty());
    unit->monster[0].revealed = 1;
    unit->monster[0].state = 1;
    EXPECT_TRUE(query(args)["entities"].empty());
    unit->monster[0].state = 2;
    position[0] = std::numeric_limits<float>::quiet_NaN();
    unit->chara[0][0].SetPosition(position);
    EXPECT_TRUE(query(args)["entities"].empty());
    args["radius"] = -1;
    EXPECT_THROW(query(args), std::runtime_error);
    args["radius"] = 10001;
    EXPECT_THROW(query(args), std::runtime_error);
    args["radius"] = 5;
    args["center"] = {0, 0};
    EXPECT_THROW(query(args), std::runtime_error);
    args["center"] = {0, 0, 0};
    args["extra"] = true;
    EXPECT_THROW(query(args), std::runtime_error);
}

TEST_F(ModPhysics, RepeatedCommandsFollowOrderAndLeaveNoRestorationOrDeferredWork) {
    ASSERT_TRUE(Apply({Set({2, 0, 0}, "first.mod"), Set({5, 0, 0}, "second.mod")}));
    Expect(5, 0, 0);
    // The command is a one-shot effect, not a persistent movement controller.
    mods::StopTrackingEntities();
    EXPECT_FLOAT_EQ(unit->monster[0].movement_speed, 5);
    unit->CleanViewMonstor(0);
    EXPECT_FLOAT_EQ(unit->monster[0].movement_speed, 0);
    EXPECT_TRUE(mods::OwnedEnemyScales().empty());
    EXPECT_TRUE(mods::OwnedEnemyStats().empty());
}

TEST_F(ModPhysics, QueriesAndEmptyCallbacksPreserveNativeBytesAndRetailRng) {
    ASSERT_TRUE(Apply({Set({3, 0, 4})}));
    float position[4] = {3, 0, 4, 1};
    unit->chara[0][0].SetPosition(position);
    std::vector<unsigned char> before(sizeof(CMonstorUnit));
    std::memcpy(before.data(), unit.get(), before.size());
    srand(9823);
    const int expected = rand();
    srand(9823);
    auto result = mods::ReadEntities(unit.get(), "physics.query", {
                                                                      {"center", {0, 0, 0}},
                                                                      {"radius", 5        }
    });
    ASSERT_EQ(result["entities"].size(), 1);
    Expect(3, 0, 4);
    ASSERT_TRUE(Apply({}));
    EXPECT_EQ(std::memcmp(before.data(), unit.get(), before.size()), 0);
    EXPECT_EQ(rand(), expected);
    result["entities"][0]["position"][0] = 100;
    EXPECT_EQ(mods::ReadEntities(unit.get(), "physics.query", {
                                                                  {"center", {0, 0, 0}},
                                                                  {"radius", 5        }
    })["entities"][0]["position"][0],
              3);
}
