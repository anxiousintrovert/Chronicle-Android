#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>

#include "dun/gameloop.hpp"
#include "editmapscript.hpp"
#include "gameloop.hpp"
#include "gamemode.hpp"
#include "gfx_fixture.hpp"
#include "mainitemmodel.hpp"
#include "mods/quick_items.hpp"
#include "mods/weapon_loadout.hpp"
#include "platform/clock.hpp"
#include "platform/config.hpp"
#include "platform/input_script.hpp"
#include "platform/paths.hpp"
#include "savedata.hpp"

extern int start_event_no, start_system_event;

namespace {
bool        transitioned = false, queued = false, verified = false;
std::string last_state;

void QuickItemPump() {
    WindowPollEvents();
    InputPoll();
    InputScriptApply(GameFrameCount());
    if (!transitioned && mods::TownLoadoutReady()) {
        // Disposable fixture inventory, installed before native dungeon loading.
        auto     &s = *SaveData->GetDngStatus();
        const int items[] = {ITEM_REGULAR_WATER, ITEM_BREAD, ITEM_DRAN_S_FEATHER};
        for (int i = 0; i < 3; ++i) {
            s.item_pack.quick_item_slot[i] = items[i];
            s.item_pack.quick_item_qty[i] = 1;
        }
        s.active_item_vol[2] = 600;
        s.hp[0] = s.next_hp[0] = 10;
        s.max_hp[0] = 100;
        s.life_step[0] = 0;
        s.water_now[0] = 10;
        s.water_max[0] = 40;
        MapJump(200, -1);
        EdEventInfo.integer_arguments[0] = 200;
        start_event_no = -1;
        start_system_event = 2;
        transitioned = true;
    }
    if (!transitioned) {
        return;
    }
    const auto state = mods::ReadQuickItems();
    last_state = state.dump();
    if (!queued && state.value("ready", false)) {
        bool all_ready = state["slots"].size() == 3;
        for (const auto &slot : state["slots"]) {
            all_ready = all_ready && slot.value("ready", false);
        }
        if (all_ready) {
            for (const auto &action : mods::Scripts().Actions("audit.quick")) {
                if (action.id == "select" && action.available) {
                    // Native dungeon initialization may reset the town gauges.
                    UserStatus->hp[0] = UserStatus->next_hp[0] = 10;
                    UserStatus->life_step[0] = 0;
                    UserStatus->water_now[0] = 10;
                    std::string error;
                    queued = mods::Scripts().QueueAction("audit.quick", "select", error);
                    EXPECT_TRUE(queued) << error;
                }
            }
        }
    }
    if (queued) {
        const auto saved = nlohmann::json::parse(mods::Scripts().SaveState());
        const auto values = saved.at("mods").at("audit.quick").value("values", nlohmann::json::object());
        if (values.value("verified", false)) {
            EXPECT_EQ(activeItem.now, 3);
            EXPECT_EQ(UserStatus->item_pack.quick_item_qty[0], 0);
            EXPECT_EQ(UserStatus->item_pack.quick_item_qty[1], 0);
            EXPECT_EQ(UserStatus->item_pack.quick_item_qty[2], 1);
            EXPECT_LT(UserStatus->active_item_vol[2], 600);
            EXPECT_GE(UserStatus->active_item_vol[2], 596);
            EXPECT_GT(UserStatus->water_now[0], 10);
            EXPECT_LE(UserStatus->water_now[0], UserStatus->water_max[0]);
            EXPECT_GT(UserStatus->hp[0], 10);
            EXPECT_GE(activeItem.model[1], 0);  // Native drinking keeps the cached slot model.
            EXPECT_EQ(activeItem.model[8], -1); // Its temporary hand model is retired.
            EXPECT_EQ(activeItem.model[2], -1);
            verified = true;
            GameRequestStop();
        }
    }
}

TEST(ModQuickItemsRetail, SharedActionSelectsAndUsesNativeWaterFoodAndFeather) {
    const char *data = std::getenv("DC_DATA");
    const char *audit = std::getenv("DC_QUICK_ITEMS_AUDIT");
    if (!data || !audit || !*audit) {
        GTEST_SKIP() << "Run alone with DC_DATA and DC_QUICK_ITEMS_AUDIT (disposable directory)";
    }
    transitioned = queued = verified = false;
    last_state.clear();
    const std::filesystem::path root = audit;
    std::filesystem::create_directories(root / "mods/audit.quick");
    std::ofstream(root / "mods-profile.json") << R"({"schema_version":1,"enabled":{"audit.quick":true}})";
    std::ofstream(root / "mods/audit.quick/mod.json") << R"({"schema_version":1,"api_version":1,"id":"audit.quick","name":"Native quick-slot audit","version":"1.0.0","game":"chronicle","files":{},"script":"main.lua","permissions":["game.read","game.write"],"requires_capabilities":["host.calls.v1","menu.actions.v1"],"requires_operations":["items.quick_slots","items.quick_slot_select","items.quick_slot_use"]})";
    std::ofstream(root / "mods/audit.quick/main.lua") << R"(
local armed=false
local phase=1
assert(chronicle.action('select','Select native quick slots','Cycle loaded water, bread and feather slots.',function() armed=true end))
return {on_tick=function()
 if not armed or chronicle.get('verified') then return end
 local state=assert(chronicle.call('items.quick_slots'))
 if not state.ready then return end
 if phase<=4 then
  if phase>1 then assert(state.selected==phase-1,'native selection was not committed') end
  if phase<4 then
   assert(state.slots[phase].ready,state.slots[phase].reason)
   assert(chronicle.call('items.quick_slot_select',{slot=phase}))
  else assert(chronicle.call('items.quick_slot_use',{slot=1})) end
  phase=phase+1
 elseif phase==5 and state.slots[1].quantity==0 then
  assert(chronicle.call('items.quick_slot_use',{slot=2})); phase=6
 elseif phase==6 and state.slots[2].quantity==0 then
  assert(chronicle.call('items.quick_slot_use',{slot=3})); phase=7
 elseif phase==7 and not state.use_pending then chronicle.set('verified',true) end
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
    ClockAddPumpHook(QuickItemPump);
    ASSERT_TRUE(GameSetJump("edit:0"));
    GameSetFastLoad(true);
    GameSetAutosave(false);
    GameSetFrameBudget(18500);
    const int result = RunGame(0, nullptr);
    ClockRemovePumpHook(QuickItemPump);
    InputShutdown();
    EXPECT_EQ(result, 0);
    EXPECT_TRUE(transitioned);
    EXPECT_TRUE(queued) << last_state;
    EXPECT_TRUE(verified) << last_state;
    for (const auto &diagnostic : mods::Scripts().Diagnostics()) {
        ADD_FAILURE() << diagnostic;
    }
}
} // namespace
