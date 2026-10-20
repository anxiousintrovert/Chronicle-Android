#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "gameloop.hpp"
#include "gfx_fixture.hpp"
#include "mods/floor_rules.hpp"
#include "mods/party.hpp"
#include "platform/clock.hpp"
#include "platform/config.hpp"
#include "platform/input_script.hpp"
#include "platform/paths.hpp"
#include "savedata.hpp"

namespace {
bool                  queued = false, finished = false;
std::array<char, 150> table_before{};
int                   active_before = -1;

void RulesPump() {
    WindowPollEvents();
    InputPoll();
    InputScriptApply(GameFrameCount());
    if (!queued && mods::ReadPartySwitchState().value("ready", false)) {
        for (const auto &action : mods::Scripts().Actions("audit.rules")) {
            if (action.id == "check" && action.available) {
                std::memcpy(table_before.data(), SaveData->GetDngStatus()->res_limit_zone_id, 150);
                active_before = SaveData->GetDngStatus()->res_limit_zone_current;
                std::string error;
                queued = mods::Scripts().QueueAction("audit.rules", "check", error);
                EXPECT_TRUE(queued) << error;
            }
        }
    }
    if (queued) {
        const auto saved = nlohmann::json::parse(mods::Scripts().SaveState());
        const auto values = saved.at("mods").at("audit.rules").value("values", nlohmann::json::object());
        if (values.value("done", false)) {
            finished = true;
            EXPECT_EQ(std::memcmp(table_before.data(), SaveData->GetDngStatus()->res_limit_zone_id, 150), 0);
            EXPECT_EQ(SaveData->GetDngStatus()->res_limit_zone_current, active_before);
            GameRequestStop();
        }
    }
}

TEST(ModFloorRulesRetail, SharedActionSeparatesSavedAndLiveRulesAndRestoresBoth) {
    const char *data = std::getenv("DC_DATA"), *audit = std::getenv("DC_FLOOR_RULES_AUDIT");
    if (!data || !audit || !*audit) {
        GTEST_SKIP() << "Run alone with DC_DATA and DC_FLOOR_RULES_AUDIT";
    }
    queued = finished = false;
    const std::filesystem::path root = audit;
    std::filesystem::create_directories(root / "mods/audit.rules");
    std::ofstream(root / "mods-profile.json") << R"({"schema_version":1,"enabled":{"audit.rules":true}})";
    std::ofstream(root / "mods/audit.rules/mod.json") << R"({"schema_version":1,"api_version":1,"id":"audit.rules","name":"Floor rule audit","version":"1.0.0","game":"chronicle","files":{},"script":"main.lua","permissions":["game.read","game.write"],"requires_capabilities":["host.calls.v1","menu.actions.v1"],"requires_operations":["dungeon.floor_rules_get","dungeon.floor_rules_set"]})";
    std::ofstream(root / "mods/audit.rules/main.lua") << R"(
local step=0
local original=nil
local remote=nil
assert(chronicle.action('check','Test floor rules','Verify saved and active rule separation.',function()
 original=assert(chronicle.call('dungeon.floor_rules_get',{dungeon=0,floor=0}))
 remote=assert(chronicle.call('dungeon.floor_rules_get',{dungeon=0,floor=1}))
 step=1
end))
return {on_tick=function()
 if step==0 or step==6 then return end
 local current=assert(chronicle.call('dungeon.floor_rules_get',{dungeon=0,floor=0}))
 if step==1 then
  assert(chronicle.call('dungeon.floor_rules_set',{dungeon=0,floor=1,rule=11}))
 elseif step==2 then
  assert(current.active_rule==original.active_rule)
  assert(chronicle.call('dungeon.floor_rules_get',{dungeon=0,floor=1}).rule==11)
  assert(chronicle.call('dungeon.floor_rules_set',{dungeon=0,floor=0,rule=11,apply_current=true}))
 elseif step==3 then
  assert(current.active_rule==11 and current.rule==11)
  assert(chronicle.call('dungeon.floor_rules_set',{dungeon=0,floor=0,rule=original.active_rule,apply_current=true}))
 elseif step==4 then
  assert(current.active_rule==original.active_rule)
  assert(chronicle.call('dungeon.floor_rules_set',{dungeon=0,floor=0,rule=original.rule}))
  assert(chronicle.call('dungeon.floor_rules_set',{dungeon=0,floor=1,rule=remote.rule}))
 elseif step==5 then chronicle.set('done',true) end
 step=step+1
end}
)";
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
    ClockAddPumpHook(RulesPump);
    ASSERT_TRUE(GameSetJump("dungeon:0"));
    GameSetFastLoad(true);
    GameSetAutosave(false);
    GameSetFrameBudget(18500);
    const int result = RunGame(0, nullptr);
    ClockRemovePumpHook(RulesPump);
    InputShutdown();
    EXPECT_EQ(result, 0);
    EXPECT_TRUE(queued);
    EXPECT_TRUE(finished);
    for (const auto &diagnostic : mods::Scripts().Diagnostics()) {
        ADD_FAILURE() << diagnostic;
    }
}
} // namespace
