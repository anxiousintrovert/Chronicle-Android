#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>

#include "gameloop.hpp"
#include "gamemode.hpp"
#include "gfx_fixture.hpp"
#include "main.hpp"
#include "mods/weapon_loadout.hpp"
#include "mods/world_transition.hpp"
#include "platform/clock.hpp"
#include "platform/config.hpp"
#include "platform/input_script.hpp"
#include "platform/paths.hpp"
#include "savedata.hpp"

namespace {
bool        queued = false, arrived = false;
bool        from_dungeon = false, enter_dungeon = false;
std::string last_state;

void TravelPump() {
    WindowPollEvents();
    InputPoll();
    InputScriptApply(GameFrameCount());
    const auto state = mods::ReadWorldTransition();
    last_state = state.dump();
    if (!queued && state.value("ready", false)) {
        for (const auto &action : mods::Scripts().Actions("audit.travel")) {
            if (action.id == "travel" && action.available) {
                if (from_dungeon) {
                    auto &s = *SaveData->GetDngStatus();
                    s.hp[0] = s.next_hp[0] = 5;
                    s.life_step[0] = 0;
                    s.ailments[0] = AILMENT_POISON;
                }
                std::string error;
                queued = mods::Scripts().QueueAction("audit.travel", "travel", error);
                EXPECT_TRUE(queued) << error;
            }
        }
    }
    if (queued && enter_dungeon && MapNo == 200 && state.value("ready", false)) {
        arrived = true;
        EXPECT_EQ(SaveData->GetDngStatus()->cur_floor, 1);
        EXPECT_EQ(state["request"]["state"], "dispatched");
        EXPECT_EQ(mods::TakeDungeonEntryFloor(0), -1);
        GameRequestStop();
    }
    if (queued && !enter_dungeon && MapNo == 1 && mods::TownLoadoutReady()) {
        arrived = true;
        EXPECT_EQ(state["request"]["state"], "dispatched");
        EXPECT_EQ(SaveData->GetDngStatus()->cur_chara, 0);
        if (from_dungeon) {
            const auto &s = *SaveData->GetDngStatus();
            EXPECT_EQ(s.hp[0], s.max_hp[0]);
            EXPECT_EQ(s.ailments[0], 0);
        }
        GameRequestStop();
    }
}

void RunTravelAudit(bool dungeon, bool enter = false) {
    const char *data = std::getenv("DC_DATA"), *audit = std::getenv("DC_WORLD_TRANSITION_AUDIT");
    if (!data || !audit || !*audit) {
        GTEST_SKIP() << "Run alone with DC_DATA and DC_WORLD_TRANSITION_AUDIT (disposable directory)";
    }
    queued = arrived = false;
    from_dungeon = dungeon;
    enter_dungeon = enter;
    last_state.clear();
    const std::filesystem::path root = audit;
    std::filesystem::create_directories(root / "mods/audit.travel");
    std::ofstream(root / "mods-profile.json") << R"({"schema_version":1,"enabled":{"audit.travel":true}})";
    std::ofstream(root / "mods/audit.travel/mod.json") << R"({"schema_version":1,"api_version":1,"id":"audit.travel","name":"World travel audit","version":"1.0.0","game":"chronicle","files":{},"script":"main.lua","permissions":["game.read","game.write"],"requires_capabilities":["host.calls.v1","menu.actions.v1"],"requires_operations":["world.transition_state","world.transition"]})";
    std::ofstream(root / "mods/audit.travel/main.lua") << R"(
local armed=false
local requested=false
assert(chronicle.action('travel','Travel to Matataki','Request native settlement travel.',function() armed=true end))
return {on_tick=function()
 if not armed or requested then return end
 local state=assert(chronicle.call('world.transition_state'))
 if not state.ready then return end
 assert(chronicle.call('world.transition',{map=1}))
 requested=true
end}
)";
    if (enter) {
        std::ofstream(root / "mods/audit.travel/main.lua") << R"(
local armed=false
local requested=false
assert(chronicle.action('travel','Enter dungeon floor 2','Request native dungeon entry.',function() armed=true end))
return {on_tick=function()
 if not armed or requested then return end
 local state=assert(chronicle.call('world.transition_state'))
 if not state.ready then return end
 assert(chronicle.call('dungeon.enter',{dungeon=0,floor=1}))
 requested=true
end}
)";
    }
    PathsSetDataRoot(data);
    PathsSetSaveRoot(root);
    ConfigLoad();
    dc::test::GfxFixture graphics(640, 480, 1.0f, dc::test::GfxOptions{.offscreen = true});
    InputInit();
    InputScript script;
    std::string input = "0\n", error;
    for (int i = 90; i < 18100; i += 100) {
        input += std::to_string(i) + " cross\n" + std::to_string(i + 5) + "\n";
    }
    ASSERT_TRUE(InputScriptParse(input, script, error)) << error;
    InputScriptInstall(std::move(script));
    ClockSetUnbounded(true);
    ClockReset();
    ClockAddPumpHook(TravelPump);
    ASSERT_TRUE(GameSetJump(dungeon ? "dungeon:0" : "edit:0"));
    GameSetFastLoad(true);
    GameSetAutosave(false);
    GameSetFrameBudget(18500);
    const int result = RunGame(0, nullptr);
    ClockRemovePumpHook(TravelPump);
    InputShutdown();
    EXPECT_EQ(result, 0);
    EXPECT_TRUE(queued) << last_state;
    EXPECT_TRUE(arrived) << last_state;
    for (const auto &diagnostic : mods::Scripts().Diagnostics()) {
        ADD_FAILURE() << diagnostic;
    }
}

TEST(ModWorldTransitionRetail, SharedActionTravelsFromNoruneToMatataki) { RunTravelAudit(false); }

TEST(ModWorldTransitionRetail, SharedActionLeavesDungeonThroughNativeCleanup) { RunTravelAudit(true); }

TEST(ModWorldTransitionRetail, SharedActionEntersExplicitNativeDungeonFloor) { RunTravelAudit(false, true); }
} // namespace
