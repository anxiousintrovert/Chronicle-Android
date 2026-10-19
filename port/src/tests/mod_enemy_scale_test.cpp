#include <gtest/gtest.h>

#include <limits>

#include "frame.hpp"
#include "mods/entities.hpp"
#include "mods/entity_tracking.hpp"
#include "mods/runtime.hpp"
#include "monstorunit.hpp"
#include "savedata.hpp"

namespace {
class ModEnemyScale : public testing::Test {
protected:
    std::unique_ptr<CMonstorUnit> unit;
    std::unique_ptr<CSaveData>    save;
    CFrame                        model, shadow;
    std::string                   id, error;

    void SetUp() override {
        ASSERT_TRUE(mods::OwnedEnemyScales().empty());
        mods::StopTrackingEntities();
        unit = std::make_unique<CMonstorUnit>();
        save = std::make_unique<CSaveData>();
        unit->CleanViewMonstor(0);
        auto &a = unit->monster[0];
        a.state = 2;
        a.revealed = 1;
        a.hp = a.max_hp = 50;
        a.body_radius = 12;
        a.collision_radius = 18;
        unit->chara[0][0].frame = &model;
        unit->chara[0][0].shadow_frame = &shadow;
        unit->chara[0][0].SetScale(1, 2, 3);
        id = std::to_string(mods::TrackEntities(*unit).Get(0));
    }

    void TearDown() override {
        mods::ClearOwnedEnemyScales(unit.get());
        mods::ClearOwnedEnemyStats(unit.get());
        mods::StopTrackingEntities();
        EXPECT_TRUE(mods::OwnedEnemyScales().empty());
    }

    mods::GameCommand Set(double multiplier, std::string owner = "first.mod") {
        return {
            "entities.scale_set", {{"entity", id}, {"scale", multiplier}},
             owner
        };
    }

    mods::GameCommand Reset(std::string owner = "first.mod") {
        return {"entities.scale_reset", {{"entity", id}}, owner};
    }

    bool Apply(std::vector<mods::GameCommand> commands) {
        return mods::ApplyEntityAndSave(*save, unit.get(), commands, 0, error, nullptr, nullptr);
    }

    void ExpectScale(mods::EnemyScale expected) {
        EXPECT_EQ(mods::ReadEnemyScale(unit->chara[0][0]), expected);
        for (unsigned i = 0; i < 3; ++i) {
            EXPECT_EQ(model.scale[i], expected[i]);
            EXPECT_EQ(shadow.scale[i], expected[i]);
        }
    }
};
} // namespace

TEST_F(ModEnemyScale, NativeModelAndShadowChangeWithoutChangingCombatRadii) {
    ASSERT_TRUE(Apply({Set(2)})) << error;
    ExpectScale({2, 4, 6});
    EXPECT_EQ(unit->monster[0].hp, 50);
    EXPECT_EQ(unit->monster[0].body_radius, 12);
    EXPECT_EQ(unit->monster[0].collision_radius, 18);
    const auto snapshot = mods::ReadEntities(unit.get(), "entities.get", {
                                                                             {"entity", id}
    });
    EXPECT_EQ(snapshot["visual_scale"], nlohmann::json::array({2, 4, 6}));
    ASSERT_TRUE(Apply({Set(3), Set(0.5)}));
    ExpectScale({0.5, 1, 1.5}); // Relative to original, not the prior multiplier.
    ASSERT_TRUE(Apply({Reset()}));
    ExpectScale({1, 2, 3});
    EXPECT_TRUE(mods::OwnedEnemyScales().empty());
}

TEST_F(ModEnemyScale, ConflictingOwnerRejectsEntireMixedTransaction) {
    ASSERT_TRUE(Apply({Set(2)}));
    EXPECT_FALSE(Apply({
        {"entities.set_stats", {{"entity", id}, {"defense", 99}}, "second.mod"},
        Set(3, "second.mod")
    }));
    EXPECT_NE(error.find("first.mod"), std::string::npos);
    EXPECT_TRUE(mods::OwnedEnemyStats().empty());
    ExpectScale({2, 4, 6});
    ASSERT_TRUE(Apply({Reset("second.mod")}));
    ExpectScale({2, 4, 6});
}

TEST_F(ModEnemyScale, InvalidAndNonfiniteInputsNeverAcquireOwnership) {
    for (double value : {0.49, 3.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        EXPECT_FALSE(Apply({Set(value)}));
    }
    auto extra = Set(2);
    extra.arguments["owner"] = "another.mod";
    EXPECT_FALSE(Apply({extra}));
    EXPECT_FALSE(Apply({Set(2, "")}));
    auto boolean = Set(2);
    boolean.arguments["scale"] = true;
    EXPECT_FALSE(Apply({boolean}));
    EXPECT_TRUE(mods::OwnedEnemyScales().empty());
    ExpectScale({1, 2, 3});
}

TEST_F(ModEnemyScale, InvalidNativeScaleOrOverflowDoesNotPublish) {
    unit->chara[0][0].SetScale(std::numeric_limits<float>::max(), 2, 3);
    EXPECT_FALSE(Apply({Set(3)}));
    EXPECT_TRUE(mods::OwnedEnemyScales().empty());
    EXPECT_EQ(model.scale[0], std::numeric_limits<float>::max());
    unit->chara[0][0].SetScale(0, 2, 3);
    EXPECT_FALSE(Apply({Set(2)}));
    unit->chara[0][0].SetScale(std::numeric_limits<float>::quiet_NaN(), 2, 3);
    EXPECT_FALSE(Apply({Set(2)}));
    EXPECT_TRUE(mods::OwnedEnemyScales().empty());
}

TEST_F(ModEnemyScale, SaveFailureRollsBackModelShadowAndClaims) {
    save->SetFishingPoint(0);
    EXPECT_FALSE(Apply({
        Set(2), {"fishing.spend", {{"amount", 1}}}
    }));
    ExpectScale({1, 2, 3});
    EXPECT_TRUE(mods::OwnedEnemyScales().empty());
    ASSERT_TRUE(Apply({Set(2)}));
    EXPECT_FALSE(Apply({
        Reset(), {"fishing.spend", {{"amount", 1}}}
    }));
    ExpectScale({2, 4, 6});
    EXPECT_FALSE(Apply({Set(3, "second.mod")}));
}

TEST_F(ModEnemyScale, NativeScaleChangesArePreservedAndRebasedPerAxis) {
    ASSERT_TRUE(Apply({Set(2)}));
    unit->chara[0][0].SetScale(4, 4, 6); // Native changes X only.
    ASSERT_TRUE(Apply({Set(3)}));
    ExpectScale({12, 6, 9});
    unit->chara[0][0].SetScale(12, 5, 9); // Native changes Y only.
    ASSERT_TRUE(Apply({Reset()}));
    ExpectScale({4, 5, 3});
}

TEST_F(ModEnemyScale, SlotReuseAndFloorClearRestoreBeforeHandleExpiration) {
    ASSERT_TRUE(Apply({Set(2)}));
    mods::EntitySlotConstructed(unit.get(), 0);
    ExpectScale({1, 2, 3});
    EXPECT_TRUE(mods::OwnedEnemyScales().empty());
    EXPECT_FALSE(Apply({Set(2)}));
    id = std::to_string(mods::TrackEntities(*unit).Get(0));
    ASSERT_TRUE(Apply({Set(2)}));
    unit->CleanViewMonstor(0);
    ExpectScale({1, 2, 3});
    EXPECT_TRUE(mods::OwnedEnemyScales().empty());
}

TEST_F(ModEnemyScale, OwnerRetentionAndNoPackageGameBridgeCleanUp) {
    ASSERT_TRUE(Apply({Set(2)}));
    const std::vector<std::string> active{"first.mod"};
    mods::RetainOwnedEnemyScales(active);
    ExpectScale({2, 4, 6});
    ASSERT_FALSE(mods::Scripts().HasGameplay());
    mods::TickGame();
    ExpectScale({1, 2, 3});
    EXPECT_TRUE(mods::OwnedEnemyScales().empty());
}
