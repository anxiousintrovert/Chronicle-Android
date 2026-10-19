#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>

#include "battlemenu.hpp"
#include "btitem.hpp"
#include "gameloop.hpp"
#include "gfx_fixture.hpp"
#include "mods/party.hpp"
#include "platform/clock.hpp"
#include "platform/config.hpp"
#include "platform/input_script.hpp"
#include "platform/paths.hpp"
#include "savedata.hpp"

namespace {
bool prepared = false;
bool completed = false;
bool action_queued = false;

void AuditPump() {
    WindowPollEvents();
    InputPoll();
    InputScriptApply(GameFrameCount());
    const auto state = mods::ReadPartySwitchState();
    if (!prepared && state.value("ready", false)) {
        // Test fixture only: join the six retail members and provide their native
        // default weapons. Never runs in darkcloud or a normal unit-test run.
        auto &status = *SaveData->GetDngStatus();
        status.party_size = 6;
        for (int character = 1; character < 6; ++character) {
            status.GetItem(GetDefaultWeaponNo(character) + 1, 0);
            status.equipped_weapon_slot[character] = 0;
            status.hp[character] = status.max_hp[character] = 100;
            status.ailments[character] = 0;
        }
        prepared = true;
    }
    if (prepared) {
        const auto saved = nlohmann::json::parse(mods::Scripts().SaveState());
        const auto values = saved.at("mods").at("audit.party").value("values", nlohmann::json::object());
        if (values.value("done", false)) {
            if (!action_queued) {
                std::string error;
                action_queued = mods::Scripts().QueueAction("audit.party", "check", error);
                EXPECT_TRUE(action_queued) << error;
            }
            if (values.value("action_checked", false)) {
                completed = true;
                GameRequestStop();
            }
        }
    }
}

TEST(ModPartyRetail, SixMemberRoundTripThroughNativeResources) {
    const char *data = std::getenv("DC_DATA");
    const char *audit = std::getenv("DC_PARTY_AUDIT");
    if (!data || !audit || !*audit) {
        GTEST_SKIP() << "Run alone with DC_DATA and DC_PARTY_AUDIT (a disposable output directory)";
    }
    namespace fs = std::filesystem;
    const fs::path root = audit;
    fs::create_directories(root / "mods/audit.party");
    std::ofstream(root / "mods-profile.json") << R"({"schema_version":1,"enabled":{"audit.party":true}})";
    std::ofstream(root / "mods/audit.party/mod.json") << R"({"schema_version":1,"api_version":1,"id":"audit.party","name":"Native party audit","version":"1.0.0","game":"chronicle","files":{},"script":"main.lua","permissions":["game.read","game.write"],"requires_capabilities":["host.calls.v1","menu.actions.v1"],"requires_operations":["party.switch_state","party.set_active","weapons.refresh_state"]})";
    std::ofstream(root / "mods/audit.party/main.lua") << R"(
local phase=0
local cooldown=0
assert(chronicle.action('check','Check party switching','Verify six native character loads and return to Toan.',function()
 assert(chronicle.get('done'),'round trip has not completed')
 chronicle.set('action_checked',true)
 chronicle.log('info','PASS six native switches')
end))
return {on_tick=function()
 if chronicle.get('done') then return end
 if cooldown>0 then cooldown=cooldown-1;return end
 local r=assert(chronicle.call('party.switch_state'))
 if #r.members<6 then return end
 assert(r.request.state~='failed',r.request.reason)
 assert(r.request.state~='cancelled',r.request.reason)
 if not r.ready then return end
 if phase>0 then
  assert(r.request.state=='complete' and r.character==phase%6,'wrong loaded character')
  local weapon=assert(chronicle.call('weapons.refresh_state'))
  assert(weapon.ready and weapon.character==r.character,'weapon cache not synchronized')
 end
 if phase==6 then
  chronicle.set('done',true)
  chronicle.log('info','PASS six native switches')
 else
  phase=phase+1
  assert(chronicle.call('party.set_active',{character=phase%6}))
  cooldown=45
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
    ClockAddPumpHook(AuditPump);
    ASSERT_TRUE(GameSetJump("dungeon:0"));
    GameSetFastLoad(true);
    GameSetAutosave(false);
    GameSetFrameBudget(18500);
    const int result = RunGame(0, nullptr);
    ClockRemovePumpHook(AuditPump);
    InputShutdown();
    std::vector<std::uint8_t> pixels;
    std::uint32_t             width = 0, height = 0;
    if (GameScreenshot(pixels, width, height)) {
        gfx::WritePng(root / "result.png", pixels.data(), width, height);
    }
    for (const auto &diagnostic : mods::Scripts().Diagnostics()) {
        ADD_FAILURE() << diagnostic;
    }
    EXPECT_EQ(result, 0);
    EXPECT_TRUE(prepared);
    EXPECT_TRUE(completed) << mods::ReadPartySwitchState().dump();
    EXPECT_EQ(SaveData->GetDngStatus()->cur_chara, 0);
}
} // namespace
