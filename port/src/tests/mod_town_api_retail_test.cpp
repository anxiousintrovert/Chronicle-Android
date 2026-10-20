#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <fstream>

#include "editmapscript.hpp"
#include "gameloop.hpp"
#include "gamemode.hpp"
#include "gfx_fixture.hpp"
#include "main.hpp"
#include "mods/town.hpp"
#include "platform/clock.hpp"
#include "platform/config.hpp"
#include "platform/input_script.hpp"
#include "platform/paths.hpp"
#include "savedata.hpp"

extern int start_event_no, start_system_event;

namespace {
bool                       town_prepared = false, town_action = false, town_stopped = false, town_restored = false;
bool                       town_transition = false;
int                        dungeon_member = -1;
std::vector<unsigned char> town_equipment;

void TownAuditPump() {
    WindowPollEvents();
    InputPoll();
    InputScriptApply(GameFrameCount());
    const auto state = mods::ReadTownSelection();
    if (!town_prepared && state.value("ready", false)) {
        auto &status = *SaveData->GetDngStatus();
        // Test-only fixture: expose the retail cast without changing weapon or
        // dungeon selection. The actual requests below use Lua host calls.
        status.party_size = 6;
        dungeon_member = status.cur_chara;
        town_equipment.resize(sizeof(status.chara_weapons));
        std::memcpy(town_equipment.data(), status.chara_weapons, town_equipment.size());
        town_prepared = true;
    }
    if (town_prepared && !town_action) {
        for (const auto &action : mods::Scripts().Actions("audit.town")) {
            if (action.id == "switch" && action.available) {
                std::string error;
                town_action = mods::Scripts().QueueAction("audit.town", "switch", error);
                EXPECT_TRUE(town_action) << error;
            }
        }
    }
    if (town_action && !town_stopped) {
        const auto saved = nlohmann::json::parse(mods::Scripts().SaveState());
        const auto values = saved.at("mods").at("audit.town").value("values", nlohmann::json::object());
        if (values.value("stop_owner", false)) {
            EXPECT_EQ(state["character"], 5);
            EXPECT_EQ(state["owner"], "audit.town");
            EXPECT_GT(state["storage_bytes"].get<std::size_t>(), 0u);
            // The same runtime lifecycle used by a restart/profile change.
            // No direct call to the town cleanup function from this fixture.
            if (town_transition) {
                MapJump(1, -1);
                EdEventInfo.integer_arguments[0] = 1;
                // Queue the same native system event as the map menu. Its
                // RunSystemEvent helper is static; no production test hook.
                EXPECT_LE(start_system_event, 0);
                start_event_no = -1;
                start_system_event = 2;
            } else {
                const mods::Manager empty;
                mods::Scripts().Start(empty);
            }
            town_stopped = true;
        }
    }
    const bool finished = town_transition ? state["state"] == "retired" && MapNo == 1 && state.value("ready", false)
                                          : state["state"] == "restored";
    if (town_stopped && finished) {
        town_restored = true;
        EXPECT_EQ(state["character"], 0);
        EXPECT_EQ(state["owner"], "");
        EXPECT_EQ(state["storage_bytes"], 0);
        GameRequestStop();
    }
}

void RunTownAudit(bool transition) {
    town_prepared = town_action = town_stopped = town_restored = false;
    town_transition = transition;
    const char *data = std::getenv("DC_DATA");
    const char *audit = std::getenv("DC_TOWN_API_AUDIT");
    if (!data || !audit || !*audit) {
        GTEST_SKIP() << "Run alone with DC_DATA and DC_TOWN_API_AUDIT (disposable output folder)";
    }
    namespace fs = std::filesystem;
    const fs::path root = audit;
    fs::create_directories(root / "mods/audit.town");
    std::ofstream(root / "mods-profile.json") << R"({"schema_version":1,"enabled":{"audit.town":true}})";
    std::ofstream(root / "mods/audit.town/mod.json") << R"({"schema_version":1,"api_version":1,"id":"audit.town","name":"Town API audit","version":"1.0.0","game":"chronicle","files":{},"script":"main.lua","permissions":["game.read","game.write"],"requires_capabilities":["host.calls.v1","menu.actions.v1"],"requires_operations":["party.town_state","party.town_set_active"]})";
    std::ofstream(root / "mods/audit.town/main.lua") << R"(
local armed=false
local phase=0
local cooldown=0
local targets={1,2,3,4,5,0,5}
assert(chronicle.action('switch','Switch town characters','Exercise the retail cast and restoration.',function() armed=true end))
return {on_tick=function()
 if not armed or chronicle.get('stop_owner') then return end
 if cooldown>0 then cooldown=cooldown-1;return end
 local r=assert(chronicle.call('party.town_state'))
 assert(r.state~='failed',r.reason)
 assert(r.state~='cancelled',r.reason)
 if not r.ready or r.joined<6 then return end
 if phase>0 then
  assert(r.character==targets[phase] and r.state=='complete','wrong town body')
 end
 if phase==#targets then chronicle.set('stop_owner',true);return end
 phase=phase+1
 assert(chronicle.call('party.town_set_active',{character=targets[phase]}))
 cooldown=45
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
    ClockAddPumpHook(TownAuditPump);
    ASSERT_TRUE(GameSetJump("edit:0"));
    GameSetFastLoad(true);
    GameSetAutosave(false);
    GameSetFrameBudget(18500);
    const int result = RunGame(0, nullptr);
    ClockRemovePumpHook(TownAuditPump);
    InputShutdown();
    EXPECT_EQ(result, 0);
    EXPECT_TRUE(town_prepared);
    EXPECT_TRUE(town_action);
    EXPECT_TRUE(town_stopped);
    EXPECT_TRUE(town_restored) << mods::ReadTownSelection().dump();
    for (const auto &diagnostic : mods::Scripts().Diagnostics()) {
        ADD_FAILURE() << diagnostic;
    }
    ASSERT_NE(SaveData, nullptr);
    const auto &status = *SaveData->GetDngStatus();
    EXPECT_EQ(status.cur_chara, dungeon_member);
    if (!town_equipment.empty()) {
        EXPECT_EQ(std::memcmp(town_equipment.data(), status.chara_weapons, town_equipment.size()), 0);
    }
    std::vector<std::uint8_t> pixels;
    std::uint32_t             width = 0, height = 0;
    if (GameScreenshot(pixels, width, height)) {
        gfx::WritePng(root / "restored.png", pixels.data(), width, height);
    }
}

TEST(ModTownApiRetail, SharedActionSelectsAllBodiesAndRuntimeStopRestoresToan) { RunTownAudit(false); }

TEST(ModTownSceneRetail, NativeMapTransitionRetiresTownOwnership) { RunTownAudit(true); }
} // namespace
