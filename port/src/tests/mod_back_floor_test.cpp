#include <gtest/gtest.h>

#include <cstring>
#include <memory>

#include "btsysscript.hpp"
#include "dun/gameloop.hpp"
#include "mods/api.hpp"
#include "mods/back_floor.hpp"
#include "runscript.hpp"
#include "userstatus.hpp"

namespace {
mods::GameCommand Enter() { return {"dungeon.back_floor_enter", nlohmann::json::object(), "test.back"}; }

TEST(ModBackFloor, WaitsForFreeNativeBoundaryAndNeverReplays) {
    int                            save, map;
    mods::BackFloorContext         c{&save, &map, 0, 1};
    mods::BackFloorQueue           queue;
    std::string                    error;
    const std::vector<std::string> owners{"test.back"};
    ASSERT_TRUE(queue.Queue(c, 2, 10, {Enter()}, 0, error));
    EXPECT_FALSE(queue.Queue(c, 2, 10, {Enter()}, 0, error));
    EXPECT_NE(error.find("test.back"), std::string::npos);
    EXPECT_FALSE(queue.Step(c, 2, 11, owners));
    EXPECT_TRUE(queue.Pending());
    c.boundary = c.native_request = true;
    EXPECT_FALSE(queue.Step(c, 2, 12, owners));
    EXPECT_TRUE(queue.Pending());
    c.native_request = false;
    EXPECT_TRUE(queue.Step(c, 2, 13, owners));
    EXPECT_EQ(queue.State()["state"], "dispatched");
    EXPECT_FALSE(queue.Step(c, 2, 14, owners));
}

TEST(ModBackFloor, ContextOwnerDeadlineAndNativeArrivalRetirePendingIntent) {
    int                            save, map, other;
    const mods::BackFloorContext   original{&save, &map, 0, 1};
    const std::vector<std::string> owners{"test.back"}, none;
    for (int scenario = 0; scenario < 10; ++scenario) {
        mods::BackFloorQueue queue;
        std::string          error;
        ASSERT_TRUE(queue.Queue(original, 2, 10, {Enter()}, 0, error));
        auto c = original;
        c.boundary = true;
        if (scenario == 0) {
            c.save = &other;
        }
        if (scenario == 1) {
            c.map = &other;
        }
        if (scenario == 2) {
            ++c.dungeon;
        }
        if (scenario == 3) {
            ++c.floor;
        }
        if (scenario == 4) {
            c.unavailable = "native resources retired";
        }
        if (scenario == 5) {
            c.active = true;
            c.map = &other;
        }
        auto frame = scenario == 8 ? 1810u : scenario == 9 ? 9u
                                                           : 11u;
        EXPECT_FALSE(queue.Step(c, scenario == 6 ? 3 : 2, frame, scenario == 7 ? none : owners));
        EXPECT_FALSE(queue.Pending()) << scenario;
        EXPECT_NE(queue.State()["state"], "dispatched");
        EXPECT_FALSE(queue.Step(original, 2, 12, owners));
    }
}

TEST(ModBackFloor, RejectedBatchesDoNotPublishAndActiveBackFloorIsUnchanged) {
    int                    save, map;
    mods::BackFloorContext c{&save, &map, 0, 1};
    mods::BackFloorQueue   queue;
    std::string            error;
    EXPECT_FALSE(queue.Queue(c, 0, 0, {Enter()}, 1, error));
    EXPECT_FALSE(queue.Queue(c, 0, 0, {Enter(), Enter()}, 0, error));
    auto command = Enter();
    command.arguments["force"] = true;
    EXPECT_FALSE(queue.Queue(c, 0, 0, {command}, 0, error));
    command = Enter();
    command.owner.clear();
    EXPECT_FALSE(queue.Queue(c, 0, 0, {command}, 0, error));
    EXPECT_EQ(queue.State()["state"], "idle");
    EXPECT_FALSE(queue.Pending());
    c.active = true;
    ASSERT_TRUE(queue.Queue(c, 0, 0, {Enter()}, 0, error));
    EXPECT_EQ(queue.State()["state"], "unchanged");
    EXPECT_FALSE(queue.Pending());
}

TEST(ModBackFloor, NativeFlagQueryIsUnchangedWithoutAnOwnedTransport) {
    auto  status = std::make_unique<CUserStatus>();
    auto *previous = UserStatus;
    UserStatus = status.get();
    for (int i = 0; i < 50; ++i) {
        status->event_flags[i] = i % 3;
    }
    auto before = std::make_unique<CUserStatus>(*status);
    for (int i = 0; i < 50; ++i) {
        EXPECT_FALSE(mods::BackFloorTransportAccess(i));
        RS_STACKDATA result{};
        RS_STACKDATA args[2]{};
        args[0].type = RS_INT;
        args[0].i = i;
        args[1].type = RS_PTR;
        args[1].p = &result;
        EXPECT_EQ(_CHECK_EVENT_FLG(args, 2), 1);
        EXPECT_EQ(result.i, status->ChkEventFlag(i));
        args[0].type = RS_FLOAT;
        args[0].f = float(i) + 0.25f;
        result.i = -1;
        EXPECT_EQ(_CHECK_EVENT_FLG(args, 2), 1);
        EXPECT_EQ(result.i, status->ChkEventFlag(i));
        args[1].type = RS_INT;
        result.i = -1;
        EXPECT_EQ(_CHECK_EVENT_FLG(args, 2), 1);
        EXPECT_EQ(result.i, -1);
    }
    EXPECT_EQ(std::memcmp(status.get(), before.get(), sizeof(CUserStatus)), 0);
    UserStatus = previous;
}

TEST(ModBackFloor, PublicSchemaRejectsArgumentsAndCatalogDeclaresPermissions) {
    for (const auto name : {"dungeon.back_floor_state", "dungeon.back_floor_enter"}) {
        EXPECT_TRUE(mods::ValidateOperation(name, nlohmann::json::object()).empty());
        EXPECT_FALSE(mods::ValidateOperation(name, {
                                                       {"force", true}
        })
                         .empty());
        EXPECT_FALSE(mods::ValidateOperation(name, nlohmann::json::array()).empty());
    }
}
} // namespace
