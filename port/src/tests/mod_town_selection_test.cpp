#include <gtest/gtest.h>

#include <array>
#include <stdexcept>

#include "mods/town_selection.hpp"

namespace {
class ModTownSelection : public testing::Test {
protected:
    mods::TownSelection        selection;
    mods::TownSelectionContext context{11, 6, {}};
    std::uint64_t              epoch = 7;
    std::array<std::string, 1> owners{"town.mod"};
    std::string                error;
    std::vector<int>           loads;

    mods::GameCommand Command(int member = 1, std::string owner = "town.mod") {
        return {"party.town_set_active", {{"character", member}}, std::move(owner)};
    }

    bool Queue(int member = 1, const mods::TownSelection::Preflight &probe = {}) {
        return selection.Queue(context, epoch, {Command(member)}, 0, probe, error);
    }

    void Finish() {
        selection.Finish(context, epoch, owners, {}, [&](int member) {
            loads.push_back(member);
            return std::string{};
        });
    }
};

TEST_F(ModTownSelection, DormantQueriesAndFinishDoNotProbeLoadOrConsumeRng) {
    srand(730);
    const int next = rand();
    srand(730);
    auto       forbidden = [](int) -> std::string { throw std::runtime_error("unexpected call"); };
    const auto state = selection.State();
    selection.Finish({}, 0, {}, forbidden, forbidden);
    EXPECT_EQ(selection.State(), state);
    EXPECT_FALSE(selection.Engaged());
    EXPECT_EQ(rand(), next);
    ASSERT_TRUE(Queue(0, forbidden)) << error;
    selection.Finish(context, epoch, owners, forbidden, forbidden);
    EXPECT_EQ(selection.State()["state"], "complete");
    EXPECT_FALSE(selection.Engaged());
}

TEST_F(ModTownSelection, DefersLoadingAndOwnsSelectionUntilExplicitReturnToToan) {
    ASSERT_TRUE(Queue()) << error;
    EXPECT_TRUE(loads.empty());
    EXPECT_EQ(selection.State()["target"], 1);
    Finish();
    EXPECT_EQ(loads, std::vector<int>{1});
    EXPECT_EQ(selection.State()["owner"], "town.mod");
    EXPECT_EQ(selection.State()["character"], 1);
    Finish();
    EXPECT_EQ(loads.size(), 1);
    ASSERT_TRUE(Queue(1)) << error;
    Finish();
    EXPECT_EQ(loads.size(), 1);
    EXPECT_FALSE(selection.Queue(context, epoch, {Command(2, "other.mod")}, 0, {}, error));
    EXPECT_NE(error.find("town.mod"), std::string::npos);
    ASSERT_TRUE(Queue(0)) << error;
    Finish();
    EXPECT_EQ(loads, (std::vector<int>{1, 0}));
    EXPECT_FALSE(selection.Engaged());
}

TEST_F(ModTownSelection, StrictArgumentsAndExclusiveTransactions) {
    for (auto args : {
             nlohmann::json{{"character", 1.5}},
             nlohmann::json{{"character", -1}},
             nlohmann::json{{"character", 6}},
             nlohmann::json{{"character", true}},
             nlohmann::json{{"character", 1}, {"owner", "spoof"}}
    }) {
        auto command = Command();
        command.arguments = args;
        EXPECT_FALSE(selection.Queue(context, epoch, {command}, 0, {}, error));
    }
    EXPECT_FALSE(selection.Queue(context, epoch, {Command(1, "")}, 0, {}, error));
    EXPECT_FALSE(selection.Queue(context, epoch, {Command(), Command()}, 0, {}, error));
    EXPECT_FALSE(selection.Queue(context, epoch, {Command()}, 1, {}, error));
    context.joined = 1;
    EXPECT_FALSE(Queue());
    context.joined = 6;
    context.unavailable = "conversation active";
    EXPECT_FALSE(Queue());
    EXPECT_EQ(error, context.unavailable);
    context.unavailable.clear();
    ASSERT_TRUE(Queue()) << error;
    EXPECT_FALSE(Queue(2));
}

TEST_F(ModTownSelection, RechecksResourcesAndReadinessAtFrameBoundary) {
    EXPECT_FALSE(Queue(1, [](int) { return "missing pack"; }));
    EXPECT_FALSE(selection.Engaged());
    ASSERT_TRUE(Queue());
    selection.Finish(context, epoch, owners, [](int) { return "pack removed"; }, [](int) -> std::string { throw std::runtime_error("must not load"); });
    EXPECT_EQ(selection.State()["reason"], "pack removed");
    EXPECT_FALSE(selection.Engaged());
    ASSERT_TRUE(Queue());
    context.unavailable = "menu active";
    Finish();
    EXPECT_TRUE(loads.empty());
    EXPECT_EQ(selection.State()["reason"], "menu active");
}

TEST_F(ModTownSelection, StopsAndEpochChangesCancelRequestsAndDeferRestorationUntilSafe) {
    ASSERT_TRUE(Queue());
    owners[0] = "other.mod";
    Finish();
    EXPECT_EQ(selection.State()["state"], "cancelled");
    EXPECT_TRUE(loads.empty());
    owners[0] = "town.mod";
    ASSERT_TRUE(Queue());
    Finish();
    ++epoch;
    context.unavailable = "conversation active";
    Finish();
    EXPECT_EQ(selection.State()["state"], "restoring");
    EXPECT_TRUE(selection.State()["restore_pending"]);
    EXPECT_EQ(loads, std::vector<int>{1});
    EXPECT_FALSE(Queue(2));
    context.unavailable.clear();
    Finish();
    EXPECT_EQ(loads, (std::vector<int>{1, 0}));
    EXPECT_EQ(selection.State()["state"], "restored");
    EXPECT_FALSE(selection.Engaged());
}

TEST_F(ModTownSelection, FailedLoadsRetainCleanupObligationAndCannotTransferOwnership) {
    ASSERT_TRUE(Queue());
    selection.Finish(context, epoch, owners, {}, [](int) -> std::string { throw std::runtime_error("partial load"); });
    EXPECT_EQ(selection.State()["state"], "failed");
    EXPECT_TRUE(selection.State()["restore_pending"]);
    EXPECT_FALSE(selection.Queue(context, epoch, {Command(2, "other.mod")}, 0, {}, error));
    selection.Finish(context, epoch, owners, [](int) { return "Toan temporarily missing"; }, {});
    EXPECT_EQ(selection.State()["state"], "restoring");
    EXPECT_TRUE(selection.Engaged());
    Finish();
    EXPECT_EQ(loads, std::vector<int>{0});
    EXPECT_FALSE(selection.Engaged());
}

TEST_F(ModTownSelection, SceneMismatchDoesNotLoadIntoNewSceneOrPrematurelyReleaseOwner) {
    ASSERT_TRUE(Queue());
    Finish();
    ++context.scene;
    Finish();
    EXPECT_EQ(selection.State()["state"], "awaiting_scene_retirement");
    EXPECT_TRUE(selection.Engaged());
    EXPECT_EQ(loads, std::vector<int>{1});
    EXPECT_FALSE(Queue(2));
    selection.SceneRetired();
    EXPECT_FALSE(selection.Engaged());
    EXPECT_EQ(selection.State()["character"], 0);
    ASSERT_TRUE(Queue(2)) << error;
    Finish();
    EXPECT_EQ(loads, (std::vector<int>{1, 2}));
}

TEST_F(ModTownSelection, LostPartyMemberRestoresRetailBody) {
    ASSERT_TRUE(Queue(5));
    Finish();
    context.joined = 1;
    Finish();
    EXPECT_EQ(loads, (std::vector<int>{5, 0}));
    EXPECT_FALSE(selection.Engaged());
}
} // namespace
