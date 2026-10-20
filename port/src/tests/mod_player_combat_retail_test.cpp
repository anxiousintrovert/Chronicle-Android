#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>

#include "gameloop.hpp"
#include "gfx_fixture.hpp"
#include "mods/party.hpp"
#include "mods/player_combat.hpp"
#include "platform/clock.hpp"
#include "platform/config.hpp"
#include "platform/input_script.hpp"
#include "platform/paths.hpp"

namespace {
bool          queued = false, stopped = false, restored = false;
std::uint64_t stopped_frame = 0;
int           stopped_timer = 0;

void CombatPump() {
    WindowPollEvents();
    InputPoll();
    InputScriptApply(GameFrameCount());
    if (!queued && mods::ReadPartySwitchState().value("ready", false)) {
        for (const auto &action : mods::Scripts().Actions("audit.combat")) {
            if (action.id == "grant" && action.available) {
                std::string error;
                queued = mods::Scripts().QueueAction("audit.combat", "grant", error);
                EXPECT_TRUE(queued) << error;
            }
        }
    }
    if (queued && !stopped) {
        const auto saved = nlohmann::json::parse(mods::Scripts().SaveState());
        const auto values = saved.at("mods").at("audit.combat").value("values", nlohmann::json::object());
        if (values.value("stop_owner", false)) {
            const auto combat = mods::ReadPlayerCombat();
            ASSERT_TRUE(combat["ready"]);
            stopped_timer = combat["invincible_frames"].get<int>();
            EXPECT_GT(stopped_timer, 500);
            const mods::Manager empty;
            mods::Scripts().Start(empty);
            stopped = true;
            stopped_frame = GameFrameCount();
        }
    }
    if (stopped && GameFrameCount() > stopped_frame + 20) {
        const auto combat = mods::ReadPlayerCombat();
        ASSERT_TRUE(combat["ready"]);
        const int timer = combat["invincible_frames"].get<int>();
        EXPECT_GT(timer, 0);
        EXPECT_LT(timer, stopped_timer);
        restored = timer > 0 && timer < stopped_timer;
        GameRequestStop();
    }
}

TEST(ModPlayerCombatRetail, SharedActionExtendsTimerAndNativeCountdownSurvivesOwnerStop) {
    const char *data = std::getenv("DC_DATA");
    const char *audit = std::getenv("DC_PLAYER_COMBAT_AUDIT");
    if (!data || !audit || !*audit) {
        GTEST_SKIP() << "Run alone with DC_DATA and DC_PLAYER_COMBAT_AUDIT (disposable directory)";
    }
    queued = stopped = restored = false;
    const std::filesystem::path root = audit;
    std::filesystem::create_directories(root / "mods/audit.combat");
    std::ofstream(root / "mods-profile.json") << R"({"schema_version":1,"enabled":{"audit.combat":true}})";
    std::ofstream(root / "mods/audit.combat/mod.json") << R"({"schema_version":1,"api_version":1,"id":"audit.combat","name":"Player combat audit","version":"1.0.0","game":"chronicle","files":{},"script":"main.lua","permissions":["game.read","game.write"],"requires_capabilities":["host.calls.v1","menu.actions.v1"],"requires_operations":["player.combat_state","player.invincible"]})";
    std::ofstream(root / "mods/audit.combat/main.lua") << R"(
local ticks=0
local last=nil
assert(chronicle.action('grant','Test one-shot protection','Grant temporary invincibility and verify native countdown.',function()
 assert(chronicle.call('player.invincible',{frames=600}))
 chronicle.set('granted',true)
end))
return {on_tick=function()
 if not chronicle.get('granted') or chronicle.get('stop_owner') then return end
 local state=assert(chronicle.call('player.combat_state'))
 assert(state.ready,state.reason)
 local frames=state.invincible_frames
 assert(frames>500 and frames<=600,'timer was shortened or not committed')
 if last then assert(frames<=last,'unexpected timer extension') end
 if ticks==0 then assert(chronicle.call('player.invincible',{frames=1})) end
 ticks=ticks+1
 if ticks==60 then
  assert(frames<590,'native timer did not count down')
  chronicle.set('stop_owner',true)
 end
 last=frames
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
    ClockAddPumpHook(CombatPump);
    ASSERT_TRUE(GameSetJump("dungeon:0"));
    GameSetFastLoad(true);
    GameSetAutosave(false);
    GameSetFrameBudget(18500);
    const int result = RunGame(0, nullptr);
    ClockRemovePumpHook(CombatPump);
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
