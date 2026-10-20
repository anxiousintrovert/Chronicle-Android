#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>

#include "character.hpp"
#include "dun/gameloop.hpp"
#include "editmapscript.hpp"
#include "gameloop.hpp"
#include "gamemode.hpp"
#include "gfx_fixture.hpp"
#include "mods/party.hpp"
#include "mods/weapon_loadout.hpp"
#include "platform/clock.hpp"
#include "platform/config.hpp"
#include "platform/input_script.hpp"
#include "platform/paths.hpp"
#include "savedata.hpp"

extern int start_event_no, start_system_event;

namespace {
bool queued = false, transitioned = false, loaded = false;
int  selected_slot = -1;

void LoadoutPump() {
    WindowPollEvents();
    InputPoll();
    InputScriptApply(GameFrameCount());
    if (!queued && mods::TownLoadoutReady()) {
        for (const auto &action : mods::Scripts().Actions("audit.loadout")) {
            if (action.id == "equip" && action.available) {
                std::string error;
                queued = mods::Scripts().QueueAction("audit.loadout", "equip", error);
                EXPECT_TRUE(queued) << error;
            }
        }
    }
    if (queued && !transitioned) {
        const auto saved = nlohmann::json::parse(mods::Scripts().SaveState());
        const auto values = saved.at("mods").at("audit.loadout").value("values", nlohmann::json::object());
        if (values.value("enter_dungeon", false)) {
            selected_slot = values.at("selected_slot").get<int>();
            EXPECT_EQ(SaveData->GetDngStatus()->equipped_weapon_slot[0], selected_slot);
            MapJump(200, -1);
            EdEventInfo.integer_arguments[0] = 200;
            EXPECT_LE(start_system_event, 0);
            start_event_no = -1;
            start_system_event = 2;
            transitioned = true;
        }
    }
    if (transitioned && mods::ReadPartySwitchState().value("ready", false)) {
        EXPECT_EQ(UserStatus->equipped_weapon_slot[0], selected_slot);
        ASSERT_NE(NowWeaponHave, nullptr);
        ASSERT_NE(NowWeapon, nullptr);
        EXPECT_NE(NowWeapon->frame, nullptr);
        EXPECT_EQ(NowWeaponHave->item_no, ITEM_WEAPON_BONE_RAPIER);
        loaded = NowWeaponHave->item_no == ITEM_WEAPON_BONE_RAPIER;
        GameRequestStop();
    }
}

TEST(ModWeaponLoadoutRetail, TownEquipmentLoadsThroughNativeDungeonTransition) {
    const char *data = std::getenv("DC_DATA");
    const char *audit = std::getenv("DC_WEAPON_LOADOUT_AUDIT");
    if (!data || !audit || !*audit) {
        GTEST_SKIP() << "Run alone with DC_DATA and DC_WEAPON_LOADOUT_AUDIT (disposable directory)";
    }
    queued = transitioned = loaded = false;
    selected_slot = -1;
    const std::filesystem::path root = audit;
    std::filesystem::create_directories(root / "mods/audit.loadout");
    std::ofstream(root / "mods-profile.json") << R"({"schema_version":1,"enabled":{"audit.loadout":true}})";
    std::ofstream(root / "mods/audit.loadout/mod.json") << R"({"schema_version":1,"api_version":1,"id":"audit.loadout","name":"Weapon loadout audit","version":"1.0.0","game":"chronicle","files":{},"script":"main.lua","permissions":["game.read","game.write"],"requires_capabilities":["host.calls.v1","menu.actions.v1"],"requires_operations":["weapons.equip","weapons.equip_state","weapons.give","weapons.list"]})";
    std::ofstream(root / "mods/audit.loadout/main.lua") << R"(
local armed=false
local phase=0
local slot=nil
assert(chronicle.action('equip','Prepare town loadout','Grant a Bone Rapier and equip its exact owned slot.',function() armed=true end))
return {on_tick=function()
 if not armed or chronicle.get('enter_dungeon') then return end
 local state=assert(chronicle.call('weapons.equip_state'))
 if not state.ready then return end
 if phase==0 then
  assert(chronicle.call('weapons.give',{item=290,seed=5}))
  phase=1
 elseif phase==1 then
  for _,weapon in ipairs(assert(chronicle.call('weapons.list'))) do
   if weapon.character==0 and weapon.item==290 then assert(not slot,'ambiguous fixture');slot=weapon.slot end
  end
  assert(slot,'granted weapon missing')
  assert(chronicle.call('weapons.equip',{character=0,slot=slot}))
  phase=2
 else
  assert(state.slots[1].slot==slot and state.slots[1].item==290,'loadout not committed')
  chronicle.set('selected_slot',slot)
  chronicle.set('enter_dungeon',true)
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
    ClockAddPumpHook(LoadoutPump);
    ASSERT_TRUE(GameSetJump("edit:0"));
    GameSetFastLoad(true);
    GameSetAutosave(false);
    GameSetFrameBudget(18500);
    const int result = RunGame(0, nullptr);
    ClockRemovePumpHook(LoadoutPump);
    InputShutdown();
    EXPECT_EQ(result, 0);
    EXPECT_TRUE(queued) << mods::ReadWeaponEquipState().dump() << " georama="
                        << int(SaveData->GetDngStatus()->cur_georama) << " floor="
                        << int(SaveData->GetDngStatus()->cur_floor);
    EXPECT_TRUE(transitioned);
    EXPECT_TRUE(loaded);
    for (const auto &diagnostic : mods::Scripts().Diagnostics()) {
        ADD_FAILURE() << diagnostic;
    }
}
} // namespace
