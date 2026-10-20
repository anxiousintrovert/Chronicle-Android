#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include "editpartsinfo.hpp"
#include "editarea.hpp"
#include "editground.hpp"
#include "editloop.hpp"
#include "gameloop.hpp"
#include "gfx_fixture.hpp"
#include "main.hpp"
#include "mods/town.hpp"
#include "mods/georama_placement.hpp"
#include "platform/clock.hpp"
#include "platform/config.hpp"
#include "platform/input_script.hpp"
#include "platform/paths.hpp"
#include "savedata.hpp"

namespace {
bool request_queued, restore_queued, request_finished;
bool placement_prepared, placement_queued, removal_queued, placement_finished;
int placement_part, placement_stock, placement_count;
nlohmann::json placement_before;
void PlacementPump() {
    WindowPollEvents(); InputPoll(); InputScriptApply(GameFrameCount());
    if (!placement_prepared && mods::ReadTownSelection().value("ready", false)) {
        ASSERT_NE(pEditGround, nullptr);
        bool found = false;
        float px = 0, py = 0, pz = 0;
        for (int id = 0; id < 24 && !found; ++id) {
            auto source = pEditGround->plot_parts[id];
            if (source.handle < 0 || !source.info || source.subtype != MAP_PARTS_SUBTYPE_NONE) continue;
            for (auto *area : pEditGround->areas) {
                if (!area || found) break;
                for (int x = 0; x < area->width && !found; ++x) {
                    for (int z = 0; z < area->height && !found; ++z) {
                        px = area->offset_x + x * area->unit_size;
                        py = area->offset_y;
                        pz = area->offset_z + z * area->unit_size;
                        if (area->CheckParts(&source, px, py, pz, 0)) { found = true; placement_part = id; }
                    }
                }
            }
        }
        ASSERT_TRUE(found);
        // Provision built stock only in this fresh, disposable gameplay fixture.
        auto &info = EditPartsInfo.parts[placement_part];
        placement_count = info.placed;
        info.stock = info.placed + 1;
        placement_stock = info.stock;
        pEditGround->Save(0, SaveData); EditPartsInfo.Save(0, SaveData);
        placement_before = mods::ReadGeoramaPlacement()["placements"];
        auto state = nlohmann::json::parse(mods::Scripts().SaveState());
        state["mods"]["audit.placement"]["values"] =
            {{"part", placement_part}, {"x", px}, {"y", py}, {"z", pz}};
        std::string error;
        ASSERT_TRUE(mods::Scripts().LoadState(state.dump(), error)) << error;
        placement_prepared = true;
        return;
    }
    if (placement_prepared && !placement_queued && mods::Scripts().HasGameplay()) {
        std::string error;
        placement_queued = mods::Scripts().QueueAction("audit.placement", "place", error);
        ASSERT_TRUE(placement_queued) << error;
    }
    if (!placement_queued) return;
    const auto state = nlohmann::json::parse(mods::Scripts().SaveState());
    const int phase = state.at("mods").at("audit.placement").at("values").value("phase", 0);
    if (phase == 1 && !removal_queued) {
        const auto current = mods::ReadGeoramaPlacement()["placements"];
        ASSERT_EQ(current.size(), placement_before.size() + 1);
        EXPECT_EQ(EditPartsInfo.parts[placement_part].placed, placement_count + 1);
        EXPECT_EQ(EditPartsInfo.parts[placement_part].stock, placement_stock);
        auto saved = std::make_unique<CSaveData>(*SaveData);
        pEditGround->Save(0, saved.get()); EditPartsInfo.Save(0, saved.get());
        EXPECT_EQ(std::memcmp(saved.get(), SaveData, sizeof(CSaveData)), 0);
        std::string error;
        removal_queued = mods::Scripts().QueueAction("audit.placement", "remove", error);
        EXPECT_TRUE(removal_queued) << error;
    }
    if (phase == 2) {
        EXPECT_TRUE(removal_queued);
        EXPECT_EQ(mods::ReadGeoramaPlacement()["placements"], placement_before);
        EXPECT_EQ(EditPartsInfo.parts[placement_part].placed, placement_count);
        EXPECT_EQ(EditPartsInfo.parts[placement_part].stock, placement_stock);
        auto saved = std::make_unique<CSaveData>(*SaveData);
        pEditGround->Save(0, saved.get()); EditPartsInfo.Save(0, saved.get());
        EXPECT_EQ(std::memcmp(saved.get(), SaveData, sizeof(CSaveData)), 0);
        placement_finished = true; GameRequestStop();
    }
}
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
TEST(ModGeoramaRetail, SharedActionsPlaceRemoveAndPersistNativeTown) {
    const char *data = std::getenv("DC_DATA"), *audit = std::getenv("DC_GEORAMA_PLACEMENT_AUDIT");
    if (!data || !audit || !*audit) GTEST_SKIP() << "Requires DC_DATA and fresh DC_GEORAMA_PLACEMENT_AUDIT";
    placement_prepared = placement_queued = removal_queued = placement_finished = false;
    const std::filesystem::path root = audit;
    std::filesystem::create_directories(root / "mods/audit.placement");
    std::ofstream(root / "mods-profile.json") << R"({"schema_version":1,"enabled":{"audit.placement":true}})";
    std::ofstream(root / "mods/audit.placement/mod.json") << R"({"schema_version":1,"api_version":1,"id":"audit.placement","name":"Georama placement audit","version":"1.0.0","game":"chronicle","files":{},"script":"main.lua","permissions":["game.read","game.write"],"requires_capabilities":["host.calls.v1","menu.actions.v1"],"requires_operations":["georama.placement_state","georama.place","georama.remove"]})";
    std::ofstream(root / "mods/audit.placement/main.lua") << R"(
assert(chronicle.action('place','Place part','Place a built retail part.',function()
 local r=assert(chronicle.call('georama.placement_state',{}))
 for _,p in ipairs(r.placements) do chronicle.set('old'..p.slot,true) end
 assert(chronicle.call('georama.place',{town=0,part=math.floor(chronicle.get('part')),rotation=0,
   position={chronicle.get('x'),chronicle.get('y'),chronicle.get('z')}}))
 chronicle.set('phase',1)
end))
assert(chronicle.action('remove','Remove part','Remove the newly placed part.',function()
 local r=assert(chronicle.call('georama.placement_state',{}))
 local slot=nil
 for _,p in ipairs(r.placements) do
   if not chronicle.get('old'..p.slot) then assert(slot==nil); slot=p.slot end
 end
 assert(slot~=nil)
 assert(chronicle.call('georama.remove',{town=0,slot=slot}))
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
    ClockSetUnbounded(true); ClockReset(); ClockAddPumpHook(PlacementPump);
    ASSERT_TRUE(GameSetJump("edit:0")); GameSetFastLoad(true); GameSetAutosave(false); GameSetFrameBudget(18500);
    const int result = RunGame(0, nullptr);
    ClockRemovePumpHook(PlacementPump); InputShutdown();
    EXPECT_EQ(result, 0); EXPECT_TRUE(placement_finished);
    for (const auto &diagnostic : mods::Scripts().Diagnostics()) ADD_FAILURE() << diagnostic;
}
}
