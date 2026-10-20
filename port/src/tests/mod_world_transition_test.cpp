#include <gtest/gtest.h>

#include "main.hpp"
#include "mods/api.hpp"
#include "mods/world_transition.hpp"

namespace {
mods::GameCommand Travel(int map) { return {"world.transition", {{"map", map}}, "test.travel"}; }

TEST(ModWorldTransition, SchemaAndRetailDestinationCatalog) {
    const auto locations = mods::ReadWorldLocations();
    ASSERT_EQ(locations.size(), 5);
    for (int map = 0; map < 5; ++map) {
        EXPECT_EQ(locations[map]["map"], map);
        EXPECT_TRUE(mods::ValidateOperation("world.transition", {
                                                                    {"map", map}
        })
                        .empty());
    }
    for (nlohmann::json value : {nlohmann::json(-1), nlohmann::json(5), nlohmann::json(0.0), nlohmann::json(true)}) {
        EXPECT_FALSE(mods::ValidateOperation("world.transition", {
                                                                     {"map", value}
        })
                         .empty());
    }
    EXPECT_FALSE(mods::ValidateOperation("world.transition", {
                                                                 {"map",   0},
                                                                 {"floor", 1}
    })
                     .empty());
    EXPECT_FALSE(mods::ValidateOperation("world.transition_state", {
                                                                       {"map", 0}
    })
                     .empty());
}

TEST(ModWorldTransition, QueueStagesOnlyOneWriteAndDispatchesOnceAfterRevalidation) {
    mods::WorldTransitionQueue         queue;
    int                                save = 0, dispatches = 0, checks = 0;
    const mods::WorldTransitionContext context{&save, GAME_MODE_EDIT, 0, 0, {}};
    std::string                        error;
    auto                               assets = [&](int map) { ++checks; EXPECT_EQ(map, 1); return std::string{}; };
    EXPECT_FALSE(queue.Queue(context, 3, {Travel(1)}, 1, assets, error));
    EXPECT_FALSE(queue.Queue(context, 3, {
                                             Travel(1), {"economy.add", {{"amount", 1}}, "test.travel"}
    },
                             0, assets, error));
    EXPECT_EQ(checks, 0);
    ASSERT_TRUE(queue.Queue(context, 3, {Travel(1)}, 0, assets, error)) << error;
    EXPECT_EQ(dispatches, 0);
    EXPECT_EQ(checks, 1);
    EXPECT_FALSE(queue.Queue(context, 3, {Travel(2)}, 0, assets, error));
    std::vector<std::string> owners{"test.travel"};
    auto                     dispatch = [&](int map) { EXPECT_EQ(map, 1); ++dispatches; };
    queue.Finish(context, 3, owners, assets, dispatch);
    EXPECT_EQ(queue.State()["state"], "dispatched");
    queue.Finish(context, 3, owners, assets, dispatch);
    EXPECT_EQ(checks, 2);
    EXPECT_EQ(dispatches, 1);
}

TEST(ModWorldTransition, OwnerSceneEpochAndLateResourceFailuresPreventDispatch) {
    int                                save = 0;
    const mods::WorldTransitionContext original{&save, GAME_MODE_DUNGEON, 200, 0, {}};
    std::vector<std::string>           owners{"test.travel"};
    for (int scenario = 0; scenario < 6; ++scenario) {
        mods::WorldTransitionQueue queue;
        std::string                error;
        ASSERT_TRUE(queue.Queue(original, 4, {Travel(1)}, 0, {}, error));
        auto current = original;
        if (scenario == 1) {
            current.map = 201;
        }
        if (scenario == 2) {
            current.character = 1;
        }
        if (scenario == 3) {
            current.unavailable = "native event started";
        }
        queue.Finish(current, scenario == 4 ? 5 : 4, scenario == 0 ? std::vector<std::string>{} : owners, [&](int) { return scenario == 5 ? "missing destination resource" : ""; }, [&](int) { ADD_FAILURE() << "unexpected dispatch for scenario " << scenario; });
        EXPECT_FALSE(queue.Pending());
        EXPECT_NE(queue.State()["state"], "dispatched");
        EXPECT_FALSE(queue.State()["error"].get<std::string>().empty());
    }
}

TEST(ModWorldTransition, SameTownIsUnchangedAndUnavailableContextsReject) {
    mods::WorldTransitionQueue   queue;
    int                          save = 0;
    mods::WorldTransitionContext context{&save, GAME_MODE_EDIT, 2, 0, {}};
    std::string                  error;
    EXPECT_TRUE(queue.Queue(context, 0, {Travel(2)}, 0, [&](int) { ADD_FAILURE(); return ""; }, error));
    EXPECT_FALSE(queue.Pending());
    EXPECT_EQ(queue.State()["state"], "unchanged");
    context.unavailable = "busy";
    EXPECT_FALSE(queue.Queue(context, 0, {Travel(1)}, 0, {}, error));
    EXPECT_EQ(error, "busy");
    context.unavailable.clear();
    auto command = Travel(1);
    command.owner.clear();
    EXPECT_FALSE(queue.Queue(context, 0, {command}, 0, {}, error));
    EXPECT_FALSE(queue.Queue(context, 0, {Travel(1)}, 0, [](int) { return "missing"; }, error));
    EXPECT_FALSE(queue.Pending());
}
} // namespace
