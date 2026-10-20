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
    auto                               assets = [&](int map, int floor) { ++checks; EXPECT_EQ(map, 1); return std::string{}; };
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
    auto                     dispatch = [&](int map, int floor, std::string_view owner) { EXPECT_EQ(map, 1); ++dispatches; };
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
        queue.Finish(current, scenario == 4 ? 5 : 4, scenario == 0 ? std::vector<std::string>{} : owners, [&](int, int) { return scenario == 5 ? "missing destination resource" : ""; }, [&](int, int, std::string_view) { ADD_FAILURE() << "unexpected dispatch for scenario " << scenario; });
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
    EXPECT_TRUE(queue.Queue(context, 0, {Travel(2)}, 0, [&](int, int) { ADD_FAILURE(); return ""; }, error));
    EXPECT_FALSE(queue.Pending());
    EXPECT_EQ(queue.State()["state"], "unchanged");
    context.unavailable = "busy";
    EXPECT_FALSE(queue.Queue(context, 0, {Travel(1)}, 0, {}, error));
    EXPECT_EQ(error, "busy");
    context.unavailable.clear();
    auto command = Travel(1);
    command.owner.clear();
    EXPECT_FALSE(queue.Queue(context, 0, {command}, 0, {}, error));
    EXPECT_FALSE(queue.Queue(context, 0, {Travel(1)}, 0, [](int, int) { return "missing"; }, error));
    EXPECT_FALSE(queue.Pending());
}

TEST(ModWorldTransition, DungeonSchemaUsesNativeFloorBounds) {
    const auto locations = mods::ReadDungeonLocations();
    const int  counts[] = {15, 17, 18, 18, 15, 25, 100};
    ASSERT_EQ(locations.size(), 7);
    for (int dungeon = 0; dungeon < 7; ++dungeon) {
        EXPECT_EQ(locations[dungeon]["floor_count"], counts[dungeon]);
        EXPECT_TRUE(mods::ValidateOperation("dungeon.enter", {
                                                                 {"dungeon", dungeon}
        })
                        .empty());
        for (int floor : {0, counts[dungeon] - 1}) {
            EXPECT_TRUE(mods::ValidateOperation("dungeon.enter", {
                                                                     {"dungeon", dungeon},
                                                                     {"floor",   floor  }
            })
                            .empty());
        }
        for (int floor : {-1, counts[dungeon]}) {
            EXPECT_FALSE(mods::ValidateOperation("dungeon.enter", {
                                                                      {"dungeon", dungeon},
                                                                      {"floor",   floor  }
            })
                             .empty());
        }
    }
    for (nlohmann::json args : {
             nlohmann::json{{"dungeon", 7}},
             {{"dungeon", -1}},
             {{"dungeon", true}},
             {{"dungeon", 0.0}},
             {{"dungeon", 0}, {"floor", 1.0}},
             {{"dungeon", 0}, {"extra", 1}}
    }) {
        EXPECT_FALSE(mods::ValidateOperation("dungeon.enter", args).empty());
    }
    EXPECT_EQ(mods::TakeDungeonEntryFloor(0), -1);
    mods::PrepareDungeonEntry();
    EXPECT_EQ(mods::TakeDungeonEntryFloor(0), -1);
}

TEST(ModWorldTransition, DungeonRequestPreservesExplicitFloorAndOwnerAtDispatch) {
    int                                save = 0;
    const mods::WorldTransitionContext context{&save, GAME_MODE_EDIT, 0, 0, {}};
    const std::vector<std::string>     owners{"test.travel"};
    for (int floor : {-1, 0, 14}) {
        mods::WorldTransitionQueue queue;
        mods::GameCommand          command{"dungeon.enter", {{"dungeon", 0}}, "test.travel"};
        if (floor >= 0) {
            command.arguments["floor"] = floor;
        }
        std::string error;
        int         checks = 0, dispatched = 0;
        auto        assets = [&](int map, int requested) {
            EXPECT_EQ(map, 200);
            EXPECT_EQ(requested, floor);
            ++checks;
            return std::string{};
        };
        ASSERT_TRUE(queue.Queue(context, 1, {command}, 0, assets, error)) << error;
        EXPECT_EQ(queue.State()["floor"], floor);
        auto dispatch = [&](int map, int requested, std::string_view owner) {
            EXPECT_EQ(map, 200);
            EXPECT_EQ(requested, floor);
            EXPECT_EQ(owner, "test.travel");
            ++dispatched;
        };
        queue.Finish(context, 1, owners, assets, dispatch);
        queue.Finish(context, 1, owners, assets, dispatch);
        EXPECT_EQ(checks, 2);
        EXPECT_EQ(dispatched, 1);
    }
}

TEST(ModWorldTransition, FloorIntentCancelsOnOwnerEpochSaveDestinationOrRepeatedSceneChanges) {
    int                            save = 0, other = 0;
    const std::vector<std::string> owners{"test.travel"}, none;
    for (bool after_prepare : {false, true}) {
        for (int scenario = 0; scenario < 6; ++scenario) {
            mods::DungeonEntryRequest request;
            request.Set(&save, 3, "test.travel", 0, 1);
            if (after_prepare) {
                request.Prepare(&save, 3, 0, owners);
            }
            const void *current_save = scenario == 0 ? &other : &save;
            const auto  epoch = scenario == 1 ? 4 : 3;
            const auto  dungeon = scenario == 2 ? 1 : scenario == 3 ? -1
                                                                    : 0;
            const auto &active = scenario == 4 ? none : owners;
            if (!after_prepare || scenario == 5) {
                request.Prepare(current_save, epoch, dungeon, active);
            }
            const int expected = !after_prepare && scenario == 5 ? 1 : -1;
            EXPECT_EQ(request.Take(current_save, epoch, dungeon, active), expected) << scenario;
            EXPECT_FALSE(request.Pending());
            EXPECT_EQ(request.Take(&save, 3, 0, owners), -1);
        }
    }
    mods::DungeonEntryRequest request;
    request.Set(&save, 3, "test.travel", 0, 1);
    EXPECT_EQ(request.Take(&save, 3, 0, owners), -1); // No scene initialization yet.
    for (int floor : {-1, 15}) {
        request.Set(&save, 3, "test.travel", 0, floor);
        request.Prepare(&save, 3, 0, owners);
        EXPECT_EQ(request.Take(&save, 3, 0, owners), -1);
    }
}
} // namespace
