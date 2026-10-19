#include <gtest/gtest.h>

#include "mods/entities.hpp"
#include "mods/entity_tracking.hpp"
#include "mods/runtime.hpp"
#include "monstorunit.hpp"
#include "savedata.hpp"

namespace {
class ModEnemyStats : public testing::Test {
protected:
    std::unique_ptr<CMonstorUnit> unit;
    std::unique_ptr<CSaveData>    save;
    std::string                   id, error;

    void SetUp() override {
        ASSERT_TRUE(mods::OwnedEnemyStats().empty());
        mods::StopTrackingEntities();
        unit = std::make_unique<CMonstorUnit>();
        save = std::make_unique<CSaveData>();
        unit->CleanViewMonstor(0);
        auto &a = unit->monster[0];
        a.state = 2;
        a.revealed = 1;
        mods::WriteEnemyStats(a, {40, 80, 12, 30, 50, -1});
        id = std::to_string(mods::TrackEntities(*unit).Get(0));
    }

    void TearDown() override {
        mods::ClearOwnedEnemyStats(unit.get());
        mods::StopTrackingEntities();
        EXPECT_TRUE(mods::OwnedEnemyStats().empty());
    }

    mods::GameCommand Set(nlohmann::json fields, std::string owner = "first.mod") {
        fields["entity"] = id;
        return {"entities.set_stats", fields, owner};
    }

    mods::GameCommand Reset(std::string owner = "first.mod") {
        return {"entities.reset_stats", {{"entity", id}}, owner};
    }

    bool Apply(std::vector<mods::GameCommand> commands) {
        return mods::ApplyEntityAndSave(*save, unit.get(), commands, 0, error, nullptr, nullptr);
    }
};
} // namespace

TEST_F(ModEnemyStats, TrustedOwnerAndStrictRangesRequired) {
    for (auto fields : {nlohmann::json{{"defense", -1}}, nlohmann::json{{"hp", 0}},
                        nlohmann::json{{"reward_money", 2147483648ULL}}, nlohmann::json{{"drop_item", 131}},
                        nlohmann::json{{"drop_item", 80}}, nlohmann::json{{"unknown", 1}},
                        nlohmann::json{{"max_hp", 1.5}}, nlohmann::json{{"hp", 81}}}) {
        EXPECT_FALSE(Apply({Set(fields)})) << fields;
        EXPECT_TRUE(mods::OwnedEnemyStats().empty());
    }
    EXPECT_FALSE(Apply({
        Set({{"defense", 20}},
            "")
    }));
    EXPECT_EQ(unit->monster[0].defense, 12);
}

TEST_F(ModEnemyStats, IndependentOwnersAndConflictRollback) {
    ASSERT_TRUE(Apply({
        Set({{"defense", 24}}
        )
    }))
        << error;
    ASSERT_TRUE(Apply({
        Set({{"reward_exp", 60}},
            "second.mod")
    }))
        << error;
    EXPECT_FALSE(Apply({
        Set({{"reward_money", 100}, {"defense", 30}},
            "second.mod")
    }));
    EXPECT_NE(error.find("first.mod"), std::string::npos);
    EXPECT_NE(error.find("defense"), std::string::npos);
    EXPECT_EQ(unit->monster[0].money, 50);
    ASSERT_TRUE(Apply({Reset("second.mod")}));
    EXPECT_EQ(unit->monster[0].defense, 24);
    EXPECT_EQ(unit->monster[0].exp, 30);
    ASSERT_TRUE(Apply({Reset()}));
    EXPECT_EQ(unit->monster[0].defense, 12);
    EXPECT_TRUE(mods::OwnedEnemyStats().empty());
    ASSERT_TRUE(Apply({Reset()}));
    EXPECT_TRUE(mods::OwnedEnemyStats().empty());
}

TEST_F(ModEnemyStats, MixedSaveFailureDoesNotAcquireClaimsOrPublishValues) {
    save->SetFishingPoint(0);
    EXPECT_FALSE(Apply({
        Set({{"defense", 99}, {"max_hp", 160}}
        ), {"fishing.spend", {{"amount", 1}}}
    }));
    EXPECT_EQ(mods::ReadEnemyStats(unit->monster[0]), (mods::EnemyStatValues{40, 80, 12, 30, 50, -1}));
    EXPECT_TRUE(mods::OwnedEnemyStats().empty());
    ASSERT_TRUE(Apply({
        Set({{"defense", 25}},
            "second.mod")
    }));
    mods::EntityTransaction transaction;
    ASSERT_TRUE(transaction.Stage(unit.get(), {
                                                  Set({{"reward_exp", 80}}
                                                  )
    },
                                  error));
    EXPECT_FALSE(transaction.Stage(unit.get(), {
                                                   Set({{"hp", 0}}
                                                   )
    },
                                   error));
    transaction.Commit();
    EXPECT_EQ(unit->monster[0].exp, 30);
}

TEST_F(ModEnemyStats, MaximumHealthScalesAndRestoresWithoutUndoingCombat) {
    auto &a = unit->monster[0];
    ASSERT_TRUE(Apply({
        Set({{"max_hp", 160}}
        )
    }));
    EXPECT_EQ(a.hp, 80);
    EXPECT_EQ(a.max_hp, 160);
    a.hp = 60; // Native damage while mod tuning is active.
    ASSERT_TRUE(Apply({Reset()}));
    EXPECT_EQ(a.hp, 30);
    EXPECT_EQ(a.max_hp, 80);
    ASSERT_TRUE(Apply({
        Set({{"max_hp", 160}}
        )
    }));
    a.hp = 40;
    ASSERT_TRUE(Apply({
        Set({{"max_hp", 240}}
        )
    }));
    EXPECT_EQ(a.hp, 60);
    ASSERT_TRUE(Apply({Reset()}));
    EXPECT_EQ(a.hp, 20);
    EXPECT_EQ(a.max_hp, 80);
}

TEST_F(ModEnemyStats, HealthOwnershipCannotBeSplitAndDeathCannotBeReversed) {
    ASSERT_TRUE(Apply({
        Set({{"hp", 30}}
        )
    }));
    EXPECT_FALSE(Apply({
        Set({{"max_hp", 160}},
            "second.mod")
    }));
    ASSERT_TRUE(Apply({
        Set({{"max_hp", 160}}
        )
    }));
    unit->monster[0].hp = 0;
    mods::RetainOwnedEnemyStats({});
    EXPECT_EQ(unit->monster[0].hp, 0);
    EXPECT_EQ(unit->monster[0].max_hp, 80);
    EXPECT_TRUE(mods::OwnedEnemyStats().empty());
}

TEST_F(ModEnemyStats, ModuleStopPreservesNativeSupersedingWrites) {
    ASSERT_TRUE(Apply({
        Set({{"defense", 24}, {"reward_exp", 60}, {"drop_item", 132}}
        )
    }));
    ASSERT_TRUE(Apply({
        Set({{"reward_money", 100}},
            "second.mod")
    }));
    unit->monster[0].defense = 50; // A later native phase owns this value now.
    const std::vector<std::string> active{"second.mod"};
    mods::RetainOwnedEnemyStats(active);
    EXPECT_EQ(unit->monster[0].defense, 50);
    EXPECT_EQ(unit->monster[0].exp, 30);
    EXPECT_EQ(unit->monster[0].drop_item, -1);
    EXPECT_EQ(unit->monster[0].money, 100);
    mods::RetainOwnedEnemyStats({});
    EXPECT_EQ(unit->monster[0].money, 50);
}

TEST_F(ModEnemyStats, NativeSlotReuseAndFloorClearReleaseInheritedFields) {
    ASSERT_TRUE(Apply({
        Set({{"drop_item", 132}, {"defense", 99}}
        )
    }));
    mods::EntitySlotConstructed(unit.get(), 0);
    EXPECT_EQ(unit->monster[0].drop_item, -1);
    EXPECT_EQ(unit->monster[0].defense, 12);
    EXPECT_TRUE(mods::OwnedEnemyStats().empty());
    EXPECT_FALSE(Apply({
        Set({{"defense", 20}}
        )
    })); // old generation
    id = std::to_string(mods::TrackEntities(*unit).Get(0));
    ASSERT_TRUE(Apply({
        Set({{"reward_exp", 90}}
        )
    }));
    unit->CleanViewMonstor(0);
    EXPECT_TRUE(mods::OwnedEnemyStats().empty());
    EXPECT_EQ(unit->monster[0].exp, 30);
}

TEST_F(ModEnemyStats, OrderedHealthCommandsUseStagedMaximum) {
    ASSERT_TRUE(Apply({
        Set({{"max_hp", 160}}
        ), {"combat.heal", {{"entity", id}, {"amount", 100}}}
    }));
    EXPECT_EQ(unit->monster[0].hp, 160);
    mods::RetainOwnedEnemyStats({});
    EXPECT_EQ(unit->monster[0].hp, 80);
    EXPECT_EQ(unit->monster[0].max_hp, 80);
}

TEST_F(ModEnemyStats, GameBridgeWithoutRunningPackagesRestoresOwnedFields) {
    ASSERT_FALSE(mods::Scripts().HasGameplay());
    ASSERT_TRUE(Apply({
        Set({{"max_hp", 160}, {"defense", 99}, {"drop_item", 132}}
        )
    }));
    mods::TickGame();
    EXPECT_EQ(mods::ReadEnemyStats(unit->monster[0]), (mods::EnemyStatValues{40, 80, 12, 30, 50, -1}));
    EXPECT_TRUE(mods::OwnedEnemyStats().empty());
    mods::TickGame();
    EXPECT_EQ(mods::ReadEnemyStats(unit->monster[0]), (mods::EnemyStatValues{40, 80, 12, 30, 50, -1}));
}
