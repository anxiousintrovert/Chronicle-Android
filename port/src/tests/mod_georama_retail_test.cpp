#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <cstdlib>
#include <fstream>
#include "editpartsinfo.hpp"
#include "gameloop.hpp"
#include "gfx_fixture.hpp"
#include "main.hpp"
#include "mods/town.hpp"
#include "platform/clock.hpp"
#include "platform/config.hpp"
#include "platform/input_script.hpp"
#include "platform/paths.hpp"
#include "savedata.hpp"

namespace {
bool request_queued, restore_queued, request_finished;
int original_request;
void RequestPump() {
    WindowPollEvents(); InputPoll(); InputScriptApply(GameFrameCount());
    if (!request_queued && mods::ReadTownSelection().value("ready", false)) {
        // Establish the fresh fixture save through the same native cache writer.
        EditPartsInfo.Save(0, SaveData);
        ASSERT_GT(EditPartsInfo.parts_max, 0);
        original_request = EditPartsInfo.request[0];
        std::string error;
        request_queued = mods::Scripts().QueueAction("audit.requests", "toggle", error);
        ASSERT_TRUE(request_queued) << error;
    }
    if (!request_queued) return;
    const auto state = nlohmann::json::parse(mods::Scripts().SaveState());
    const auto values = state.at("mods").at("audit.requests").value("values", nlohmann::json::object());
    const int phase = values.value("phase", 0);
    if (phase == 1 && !restore_queued) {
        const int expected = original_request == 0 ? 1 : 0;
        EXPECT_EQ(SaveData->GetGrdData(0)->request_complete[0], expected);
        EXPECT_EQ(EditPartsInfo.request[0], expected);
        auto roundtrip = std::make_unique<CSaveData>(*SaveData);
        EditPartsInfo.Save(0, roundtrip.get());
        EXPECT_EQ(roundtrip->GetGrdData(0)->request_complete[0], expected);
        std::string error;
        restore_queued = mods::Scripts().QueueAction("audit.requests", "restore", error);
        EXPECT_TRUE(restore_queued) << error;
    }
    if (phase == 2) {
        EXPECT_TRUE(restore_queued);
        EXPECT_EQ(SaveData->GetGrdData(0)->request_complete[0], original_request != 0 ? 1 : 0);
        EXPECT_EQ(EditPartsInfo.request[0], original_request != 0 ? 1 : 0);
        request_finished = true;
        GameRequestStop();
    }
}

TEST(ModGeoramaRetail, SharedActionsUpdateRequestCacheAndSurviveNativeSave) {
    const char *data = std::getenv("DC_DATA"), *audit = std::getenv("DC_GEORAMA_REQUEST_AUDIT");
    if (!data || !audit || !*audit) GTEST_SKIP() << "Requires DC_DATA and fresh DC_GEORAMA_REQUEST_AUDIT";
    request_queued = restore_queued = request_finished = false;
    const std::filesystem::path root = audit;
    std::filesystem::create_directories(root / "mods/audit.requests");
    std::ofstream(root / "mods-profile.json") << R"({"schema_version":1,"enabled":{"audit.requests":true}})";
    std::ofstream(root / "mods/audit.requests/mod.json") << R"({"schema_version":1,"api_version":1,"id":"audit.requests","name":"Georama request audit","version":"1.0.0","game":"chronicle","files":{},"script":"main.lua","permissions":["game.read","game.write"],"requires_capabilities":["host.calls.v1","menu.actions.v1"],"requires_operations":["georama.satisfaction_get","georama.request_set"]})";
    std::ofstream(root / "mods/audit.requests/main.lua") << R"(
assert(chronicle.action('toggle','Toggle request','Test native request cache.',function()
 local r=assert(chronicle.call('georama.satisfaction_get',{town=0}))
 local original=r.requests[1].completed
 chronicle.set('original',original)
 assert(chronicle.call('georama.request_set',{town=0,request=0,completed=not original}))
 chronicle.set('phase',1)
end))
assert(chronicle.action('restore','Restore request','Restore original completion.',function()
 assert(chronicle.call('georama.request_set',{town=0,request=0,completed=chronicle.get('original')}))
 chronicle.set('phase',2)
end))
return {}
)";
    PathsSetDataRoot(data); PathsSetSaveRoot(root); ConfigLoad();
    dc::test::GfxFixture graphics(640, 480, 1.0f, dc::test::GfxOptions{.offscreen = true});
    InputInit();
    InputScript script;
    std::string input = "0\n", error;
    for (int i = 90; i < 18100; i += 100) input += std::to_string(i) + " cross\n" + std::to_string(i + 5) + "\n";
    ASSERT_TRUE(InputScriptParse(input, script, error)); InputScriptInstall(std::move(script));
    ClockSetUnbounded(true); ClockReset(); ClockAddPumpHook(RequestPump);
    ASSERT_TRUE(GameSetJump("edit:0")); GameSetFastLoad(true); GameSetAutosave(false); GameSetFrameBudget(18500);
    const int result = RunGame(0, nullptr);
    ClockRemovePumpHook(RequestPump); InputShutdown();
    EXPECT_EQ(result, 0); EXPECT_TRUE(request_finished);
    for (const auto &diagnostic : mods::Scripts().Diagnostics()) ADD_FAILURE() << diagnostic;
}
}
