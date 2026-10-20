#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>

#include "gameloop.hpp"
#include "gfx_fixture.hpp"
#include "mods/party.hpp"
#include "mods/player_tuning_native.hpp"
#include "platform/clock.hpp"
#include "platform/config.hpp"
#include "platform/input_script.hpp"
#include "platform/paths.hpp"

namespace {
bool          queued = false, stopped = false, restored = false;
std::uint64_t stopped_frame = 0;

void TuningPump() {
    WindowPollEvents();
    InputPoll();
    InputScriptApply(GameFrameCount());
    if (!queued && mods::ReadPartySwitchState().value("ready", false)) {
        for (const auto &action : mods::Scripts().Actions("audit.tuning")) {
            if (action.id == "tune" && action.available) {
                std::string error;
                queued = mods::Scripts().QueueAction("audit.tuning", "tune", error);
                EXPECT_TRUE(queued) << error;
            }
        }
    }
    if (queued && !stopped) {
        const auto saved = nlohmann::json::parse(mods::Scripts().SaveState());
        const auto values = saved.at("mods").at("audit.tuning").value("values", nlohmann::json::object());
        if (values.value("stop_owner", false)) {
            EXPECT_TRUE(mods::OwnedPlayerTuning().Engaged());
            EXPECT_EQ(mods::OwnedPlayerTuning().Value(mods::PlayerTuning::RunSpeed), 2);
            const mods::Manager empty;
            mods::Scripts().Start(empty);
            stopped = true;
            stopped_frame = GameFrameCount();
        }
    }
    if (stopped && GameFrameCount() > stopped_frame) {
        EXPECT_FALSE(mods::OwnedPlayerTuning().Engaged());
        EXPECT_EQ(mods::OwnedPlayerTuning().Value(mods::PlayerTuning::RunSpeed), 1);
        restored = !mods::OwnedPlayerTuning().Engaged();
        GameRequestStop();
    }
}

TEST(ModPlayerTuningApiRetail, SharedActionHostCallsClearAndRuntimeStopRetiresTuning) {
    const char *data = std::getenv("DC_DATA");
    const char *audit = std::getenv("DC_PLAYER_TUNING_API_AUDIT");
    if (!data || !audit || !*audit) {
        GTEST_SKIP() << "Run alone with DC_DATA and DC_PLAYER_TUNING_API_AUDIT (disposable directory)";
    }
    queued = stopped = restored = false;
    const std::filesystem::path root = audit;
    std::filesystem::create_directories(root / "mods/audit.tuning");
    std::ofstream(root / "mods-profile.json") << R"({"schema_version":1,"enabled":{"audit.tuning":true}})";
    std::ofstream(root / "mods/audit.tuning/mod.json") << R"({"schema_version":1,"api_version":1,"id":"audit.tuning","name":"Player tuning audit","version":"1.0.0","game":"chronicle","files":{},"script":"main.lua","permissions":["game.read","game.write"],"requires_capabilities":["host.calls.v1","menu.actions.v1"],"requires_operations":["player.tuning_state","player.tuning_set","player.tuning_clear"]})";
    std::ofstream(root / "mods/audit.tuning/main.lua") << R"(
local armed=false
local phase=0
local wait=0
local values={run_speed=2,attack_anim_speed=2,recoil_scale=0.5,gauge_refill=2,
 thirst_drain=0.5,durability_wear=0.5,invincible_bonus_frames=30,map_reveal=true,monster_reveal=true}
assert(chronicle.action('tune','Test player tuning','Apply owned tuning, clear, and apply again.',function() armed=true end))
return {on_tick=function()
 if not armed or chronicle.get('stop_owner') then return end
 if wait>0 then wait=wait-1;return end
 local state=assert(chronicle.call('player.tuning_state'))
 if phase==0 or phase==2 then
  assert(not state.active,'unexpected retained tuning')
  assert(chronicle.call('player.tuning_set',values))
  phase=phase+1;wait=60
 else
  assert(state.active,'tuning not published')
  for key,value in pairs(values) do
   assert(state.effective[key]==value,'incorrect tuning '..key)
   assert(state.owners[key]=='audit.tuning','incorrect owner '..key)
  end
  if phase==1 then
   assert(chronicle.call('player.tuning_clear'))
   phase=2;wait=10
  else chronicle.set('stop_owner',true) end
 end
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
    ClockAddPumpHook(TuningPump);
    ASSERT_TRUE(GameSetJump("dungeon:0"));
    GameSetFastLoad(true);
    GameSetAutosave(false);
    GameSetFrameBudget(18500);
    const int result = RunGame(0, nullptr);
    ClockRemovePumpHook(TuningPump);
    InputShutdown();
    EXPECT_EQ(result, 0);
    EXPECT_TRUE(queued);
    EXPECT_TRUE(stopped);
    EXPECT_TRUE(restored);
    for (const auto &diagnostic : mods::Scripts().Diagnostics()) {
        ADD_FAILURE() << diagnostic;
    }
}
} // namespace
