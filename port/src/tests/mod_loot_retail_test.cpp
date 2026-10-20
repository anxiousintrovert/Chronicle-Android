#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "dun/gameloop.hpp"
#include "gameloop.hpp"
#include "gfx_fixture.hpp"
#include "main.hpp"
#include "mods/loot.hpp"
#include "mods/party.hpp"
#include "mods/world_transition.hpp"
#include "monstorunit.hpp"
#include "platform/clock.hpp"
#include "platform/config.hpp"
#include "platform/input_script.hpp"
#include "platform/paths.hpp"
#include "savedata.hpp"

namespace {
bool                           queued = false, finished = false, loaded = false;
std::array<MONSTOR_MODEL, 167> table_before{};

void LootPump() {
    WindowPollEvents();
    InputPoll();
    InputScriptApply(GameFrameCount());
    if (!queued && mods::ReadWorldTransition().value("ready", false)) {
        for (const auto &action : mods::Scripts().Actions("audit.loot")) {
            if (action.id == "check" && action.available) {
                std::memcpy(table_before.data(), MonstorTable, sizeof(MonstorTable));
                std::string error;
                queued = mods::Scripts().QueueAction("audit.loot", "check", error);
                EXPECT_TRUE(queued) << error;
            }
        }
    }
    if (queued) {
        const auto saved = nlohmann::json::parse(mods::Scripts().SaveState());
        const auto values = saved.at("mods").at("audit.loot").value("values", nlohmann::json::object());
        if (MapNo == 200 && mods::ReadPartySwitchState().value("ready", false)) {
            int checked = 0;
            for (const auto &monster : NowMonstorUnit->monster) {
                if (monster.state != 2) {
                    continue;
                }
                ++checked;
                EXPECT_EQ(monster.rare_item, ITEM_REPAIR_POWDER);
                EXPECT_EQ(monster.steal_item, ITEM_BREAD);
                EXPECT_EQ(NowMonstorUnit->model[monster.base_model].rare_item, ITEM_REPAIR_POWDER);
                EXPECT_EQ(NowMonstorUnit->model[monster.base_model].steal_item, ITEM_BREAD);
            }
            EXPECT_GT(checked, 0);
            loaded = checked > 0;
        }
        if (values.value("done", false)) {
            finished = true;
            EXPECT_TRUE(loaded);
            EXPECT_FALSE(mods::HasOwnedLoot());
            EXPECT_EQ(std::memcmp(table_before.data(), MonstorTable, sizeof(MonstorTable)), 0);
            GameRequestStop();
        }
    }
}

TEST(ModLootRetail, SharedActionChangesFutureLoadsAndRestoresDefinitionsOnly) {
    const char *data = std::getenv("DC_DATA"), *audit = std::getenv("DC_LOOT_AUDIT");
    if (!data || !audit || !*audit) {
        GTEST_SKIP() << "Run alone with DC_DATA and DC_LOOT_AUDIT";
    }
    queued = finished = loaded = false;
    const std::filesystem::path root = audit;
    std::filesystem::create_directories(root / "mods/audit.loot");
    std::ofstream(root / "mods-profile.json") << R"({"schema_version":1,"enabled":{"audit.loot":true}})";
    std::ofstream(root / "mods/audit.loot/mod.json") << R"({"schema_version":1,"api_version":1,"id":"audit.loot","name":"Loot audit","version":"1.0.0","game":"chronicle","files":{},"script":"main.lua","permissions":["game.read","game.write"],"requires_capabilities":["host.calls.v1","menu.actions.v1"],"requires_operations":["dungeon.loot_get","dungeon.loot_set","dungeon.loot_reset","dungeon.enter"]})";
    std::ofstream(root / "mods/audit.loot/main.lua") << R"(
local armed=false
local next_id=0
local phase=0
assert(chronicle.action('check','Test loot definitions','Load and restore owned loot definitions.',function() armed=true end))
return {on_tick=function()
 if not armed or phase==4 then return end
 if not assert(chronicle.call('world.transition_state')).ready then return end
 if phase==0 then
  for id=next_id,math.min(next_id+31,166) do
   assert(chronicle.call('dungeon.loot_set',{monster=id,rare_item=177,steal_item=148}))
  end
  next_id=next_id+32
  if next_id>166 then phase=1 end
 elseif phase==1 then
  assert(chronicle.call('dungeon.enter',{dungeon=0,floor=0}))
  phase=2
 elseif phase==2 then
  if not assert(chronicle.call('party.switch_state')).ready then return end
  assert(chronicle.call('dungeon.loot_get',{monster=0}).rare_item==177)
  next_id=0
  phase=3
 elseif phase==3 then
  for id=next_id,math.min(next_id+31,166) do
   assert(chronicle.call('dungeon.loot_reset',{monster=id,field='rare_item'}))
   assert(chronicle.call('dungeon.loot_reset',{monster=id,field='steal_item'}))
  end
  next_id=next_id+32
  if next_id>166 then phase=4 chronicle.set('done',true) end
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
    ClockAddPumpHook(LootPump);
    ASSERT_TRUE(GameSetJump("edit:0"));
    GameSetFastLoad(true);
    GameSetAutosave(false);
    GameSetFrameBudget(18500);
    const int result = RunGame(0, nullptr);
    ClockRemovePumpHook(LootPump);
    InputShutdown();
    EXPECT_EQ(result, 0);
    EXPECT_TRUE(queued);
    EXPECT_TRUE(finished);
    for (const auto &diagnostic : mods::Scripts().Diagnostics()) {
        ADD_FAILURE() << diagnostic;
    }
}
} // namespace
