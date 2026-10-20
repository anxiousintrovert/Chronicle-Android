#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "character.hpp"
#include "chronicle/dungeon_build.hpp"
#include "chronicle/dungeon_prepare.hpp"
#include "chronicle/random_state.hpp"
#include "dun/gameloop.hpp"
#include "dungeoneventman.hpp"
#include "dungeonmap.hpp"
#include "gameloop.hpp"
#include "gamemode.hpp"
#include "gfx/gfx.hpp"
#include "gfx_fixture.hpp"
#include "main.hpp"
#include "mods/back_floor.hpp"
#include "mods/world_transition.hpp"
#include "platform/clock.hpp"
#include "platform/config.hpp"
#include "platform/input_script.hpp"
#include "platform/paths.hpp"
#include "savedata.hpp"

extern s32         gameTask;
extern CDungeonMap MainDungeonMap, UraDungeonMap;

namespace {
bool               queued = false, arrived = false;
bool               expected_expanded = false, road_captured = false;
int                expected_rooms = 6;
int                stable = 0, maximum_stable = 0;
bool               input_released = false;
std::string        last_state;
ITEM_PACK          inventory_before{};
std::array<s8, 50> flags_before{};

template <typename T>
std::vector<unsigned char> ObjectBytes(const T &value) {
    std::vector<unsigned char> bytes(sizeof(value));
    std::memcpy(bytes.data(), static_cast<const void *>(&value), bytes.size());
    return bytes;
}

void BackPump() {
    WindowPollEvents();
    InputPoll();
    InputScriptApply(GameFrameCount());
    const auto state = mods::ReadBackFloor();
    last_state = state.dump();
    if (std::getenv("DC_EXPANDED_BACK_FLOOR_AUDIT") && GameFrameCount() % 100 == 0) {
        float position[4]{};
        CharaMain.GetPosition(position);
        std::ofstream trace(std::filesystem::path(std::getenv("DC_BACK_FLOOR_AUDIT")) / "frames.log", std::ios::app);
        if (!road_captured && queued && gameTask == GAME_TASK_SCRIPT_RUN && std::abs(position[0] - (expected_expanded ? 7520.0f : 3040.0f)) < 40.0f) {
            std::vector<uint8_t> pixels;
            uint32_t             width, height;
            if (gfx::ReadbackFrame(pixels, width, height)) {
                gfx::WritePng((std::filesystem::path(std::getenv("DC_BACK_FLOOR_AUDIT")) / "road.png").string().c_str(), pixels.data(), width, height);
                road_captured = true;
            }
        }
        trace << GameFrameCount() << " task=" << gameTask << " script=" << BtEventInfo.script_no << " pos=" << position[0] << "," << position[1] << "," << position[2] << " " << last_state << "\n";
    }
    if (!queued && state.value("ready", false)) {
        for (const auto &action : mods::Scripts().Actions("audit.back")) {
            if (action.id == "enter" && action.available) {
                if (expected_expanded) {
                    EXPECT_EQ(MainDungeonMap.GridExtent(), 48);
                    EXPECT_EQ(UraDungeonMap.GridExtent(), 48);
                    if (expected_rooms > 16) {
                        EXPECT_GT(MainDungeonMap.room_num, 16);
                        EXPECT_GT(UraDungeonMap.room_num, 16);
                        const auto front_before = ObjectBytes(MainDungeonMap);
                        const auto back_before = ObjectBytes(UraDungeonMap);
                        const auto events_snapshot = ObjectBytes(DngEventMan);
                        const auto status_before = ObjectBytes(*SaveData->GetDngStatus());
                        const auto scratch_before = CaptureDungeonBuildScratch();
                        const auto rng_before = chronicle::RandomState();
                        const auto map_before = NowDngMap;
                        const auto events_before = NowEventMan;
                        const auto user_before = UserStatus;
                        auto       staged = chronicle::dungeon::PrepareMaps(MainDungeonMap, DngEventMan, *SaveData->GetDngStatus(), {64, 46, 3, 6});
                        EXPECT_GT(staged.front->room_num, 16);
                        EXPECT_GT(staged.back->room_num, 16);
                        EXPECT_THROW(chronicle::dungeon::PrepareMaps(MainDungeonMap, DngEventMan, *SaveData->GetDngStatus(), {2, 8, 6, 6}), std::runtime_error);
                        auto invalid = std::make_unique<CDungeonEventMan>(DngEventMan);
                        invalid->slot[0].enabled = 1;
                        invalid->slot[0].parts_id = 0;
                        invalid->slot[0].placement_frame = nullptr;
                        EXPECT_THROW(chronicle::dungeon::PrepareMaps(MainDungeonMap, *invalid, *SaveData->GetDngStatus(), {64, 46, 3, 6}), std::runtime_error);
                        EXPECT_EQ(ObjectBytes(DngEventMan), events_snapshot);
                        EXPECT_EQ(ObjectBytes(MainDungeonMap), front_before);
                        EXPECT_EQ(ObjectBytes(UraDungeonMap), back_before);
                        EXPECT_EQ(ObjectBytes(*SaveData->GetDngStatus()), status_before);
                        EXPECT_EQ(chronicle::RandomState(), rng_before);
                        EXPECT_EQ(CaptureDungeonBuildScratch(), scratch_before);
                        EXPECT_EQ(NowDngMap, map_before);
                        EXPECT_EQ(NowEventMan, events_before);
                        EXPECT_EQ(UserStatus, user_before);
                    }
                }
                std::string error;
                inventory_before = SaveData->GetDngStatus()->item_pack;
                std::copy_n(SaveData->GetDngStatus()->event_flags, 50, flags_before.begin());
                queued = mods::Scripts().QueueAction("audit.back", "enter", error);
                EXPECT_TRUE(queued) << error;
            }
        }
    }
    if (queued && state.value("active", false) && gameTask == GAME_TASK_PLAY) {
        if (!input_released) {
            InputScript release;
            std::string error;
            EXPECT_TRUE(InputScriptParse("0\n", release, error));
            InputScriptInstall(std::move(release));
            input_released = true;
        }
        ++stable;
        maximum_stable = std::max(maximum_stable, stable);
        if (stable >= 90) {
            if (expected_expanded) {
                float pos[4]{};
                CharaMain.GetPosition(pos);
                if (!std::isfinite(pos[0]) || !std::isfinite(pos[1]) || !std::isfinite(pos[2]) ||
                    pos[0] < -80 || pos[0] >= 48 * 160 - 80 || pos[2] < -80 || pos[2] >= 48 * 160 - 80) {
                    ADD_FAILURE() << "player ended outside expanded map: " << pos[0] << "," << pos[1] << "," << pos[2];
                    GameRequestStop();
                    return;
                }
                const int x = static_cast<int>((pos[0] + 80) / 160), z = static_cast<int>((pos[2] + 80) / 160);
                EXPECT_GE(x, 0);
                EXPECT_GE(z, 0);
                EXPECT_LT(x, 48);
                EXPECT_LT(z, 48);
                if (x >= 0 && x < 48 && z >= 0 && z < 48) {
                    EXPECT_NE(UraDungeonMap.cells[x + z * 48].parts_no, MAP_PARTS_NONE);
                }
                EXPECT_GT(pos[1], -100.0f);
            }
            arrived = true;
            EXPECT_EQ(state["request"]["state"], "dispatched");
            EXPECT_FALSE(state.value("pending", true));
            EXPECT_EQ(std::memcmp(&inventory_before, &SaveData->GetDngStatus()->item_pack, sizeof(inventory_before)), 0);
            EXPECT_TRUE(std::equal(flags_before.begin(), flags_before.end(), SaveData->GetDngStatus()->event_flags));
            EXPECT_FALSE(mods::BackFloorTransportAccess(1));
            GameRequestStop();
        }
    } else {
        stable = 0;
    }
}

void RunBackAudit(int dungeon, bool expanded = false, int rooms = 6) {
    if (expanded && !std::getenv("DC_EXPANDED_BACK_FLOOR_AUDIT")) {
        GTEST_SKIP() << "Requires explicit expanded-floor audit opt-in";
    }
    const char *data = std::getenv("DC_DATA"), *audit = std::getenv("DC_BACK_FLOOR_AUDIT");
    if (!data || !audit || !*audit) {
        GTEST_SKIP() << "Run alone with DC_DATA and DC_BACK_FLOOR_AUDIT";
    }
    queued = arrived = false;
    expected_expanded = expanded;
    expected_rooms = rooms;
    road_captured = false;
    stable = maximum_stable = 0;
    input_released = false;
    const std::filesystem::path root = audit;
    std::filesystem::create_directories(root / "mods/audit.back");
    std::ofstream(root / "mods-profile.json") << R"({"schema_version":1,"enabled":{"audit.back":true}})";
    std::ofstream(root / "mods/audit.back/mod.json") << R"({"schema_version":1,"api_version":1,"id":"audit.back","name":"Back-floor audit","version":"1.0.0","game":"chronicle","files":{},"script":"main.lua","permissions":["game.read","game.write"],"requires_capabilities":["host.calls.v1","menu.actions.v1"],"requires_operations":["dungeon.back_floor_state","dungeon.back_floor_enter"]})";
    std::ofstream(root / "mods/audit.back/main.lua") << R"(
local armed=false
local requested=false
assert(chronicle.action('enter','Enter back floor','Request native back-floor entry.',function() armed=true end))
return {on_tick=function()
 if not armed or requested then return end
 local state=assert(chronicle.call('dungeon.back_floor_state'))
 if not state.ready then return end
 assert(chronicle.call('dungeon.back_floor_enter'))
 requested=true
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
    ClockAddPumpHook(BackPump);
    ASSERT_TRUE(GameSetJump(("dungeon:" + std::to_string(dungeon)).c_str()));
    GameSetFastLoad(true);
    GameSetAutosave(false);
    GameSetFrameBudget(18500);
    std::unique_ptr<chronicle::dungeon::BuildScope> layout;
    if (expanded) {
        layout = std::make_unique<chronicle::dungeon::BuildScope>(chronicle::dungeon::BuildLayout{rooms, 46, 3, rooms > 6 ? 6 : 4});
    }
    const int result = RunGame(0, nullptr);
    layout.reset();
    ClockRemovePumpHook(BackPump);
    InputShutdown();
    EXPECT_EQ(result, 0);
    EXPECT_TRUE(queued) << last_state;
    EXPECT_TRUE(arrived) << last_state << " task=" << gameTask << " longest_play=" << maximum_stable;
    for (const auto &diagnostic : mods::Scripts().Diagnostics()) {
        ADD_FAILURE() << diagnostic;
    }
}

TEST(ModBackFloorRetail, SharedActionReachesBackFloorThroughNativeScriptBoundary) { RunBackAudit(0); }

TEST(ModBackFloorRetail, WiseOwlTransport) { RunBackAudit(1); }

TEST(ModBackFloorRetail, ShipwreckTransport) { RunBackAudit(2); }

TEST(ModBackFloorRetail, TempleTransport) { RunBackAudit(3); }

TEST(ModBackFloorRetail, MoonSeaTransport) { RunBackAudit(4); }

TEST(ModBackFloorRetail, GalleryTransport) { RunBackAudit(5); }

TEST(ModBackFloorRetail, DemonShaftTransport) { RunBackAudit(6); }

TEST(ModBackFloorRetail, ExpandedCaveTransport) { RunBackAudit(0, true); }

TEST(ModBackFloorRetail, ExpandedWiseOwlTransport) { RunBackAudit(1, true); }

TEST(ModBackFloorRetail, ExpandedShipwreckTransport) { RunBackAudit(2, true); }

TEST(ModBackFloorRetail, ExpandedTempleTransport) { RunBackAudit(3, true); }

TEST(ModBackFloorRetail, ExpandedMoonSeaTransport) { RunBackAudit(4, true); }

TEST(ModBackFloorRetail, ExpandedGalleryTransport) { RunBackAudit(5, true); }

TEST(ModBackFloorRetail, ExpandedDemonShaftTransport) { RunBackAudit(6, true); }

TEST(ModBackFloorRetail, MaximumCaveTransport) { RunBackAudit(0, true, 64); }

TEST(ModBackFloorRetail, MaximumWiseOwlTransport) { RunBackAudit(1, true, 64); }

TEST(ModBackFloorRetail, MaximumShipwreckTransport) { RunBackAudit(2, true, 64); }

TEST(ModBackFloorRetail, MaximumTempleTransport) { RunBackAudit(3, true, 64); }

TEST(ModBackFloorRetail, MaximumMoonSeaTransport) { RunBackAudit(4, true, 64); }

TEST(ModBackFloorRetail, MaximumGalleryTransport) { RunBackAudit(5, true, 64); }

TEST(ModBackFloorRetail, MaximumDemonShaftTransport) { RunBackAudit(6, true, 64); }
} // namespace
