#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include "character.hpp"
#include "chronicle/dungeon_build.hpp"
#include "chronicle/dungeon_prepare.hpp"
#include "chronicle/random_state.hpp"
#include "chronicle/monster_collision.hpp"
#include "chronicle/monster_population.hpp"
#include "chronicle/monster_audio.hpp"
#include "collisiondata.hpp"
#include "dun/gameloop.hpp"
#include "dungeoneventman.hpp"
#include "dungeonmap.hpp"
#include "gameloop.hpp"
#include "gamemode.hpp"
#include "gfx/gfx.hpp"
#include "gfx_fixture.hpp"
#include "main.hpp"
#include "itemdata.hpp"
#include "mods/back_floor.hpp"
#include "mods/camera.hpp"
#include "mods/dungeon_layout.hpp"
#include "monstorunit.hpp"
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
int                expected_monsters = 5;
bool               managed_layout = false;
int                audit_population = 0;
bool               population_injected = false;
bool               high_slot_seen = false;
bool               quit_with_population = false;
bool               fail_with_population = false;
bool               audit_partial_population = false;
bool               audit_chest_loot = false;
bool audit_camera_write = false, camera_applied = false, camera_released = false;
bool audit_lighting_write = false;
std::array<int, 3> lighting_red{-1, -1, -1};
std::uint64_t      population_frame = 0;
int                stable = 0, maximum_stable = 0;
bool               input_released = false;
std::string        last_state;
ITEM_PACK          inventory_before{};
std::array<s8, 50> flags_before{};

void AuditChestLoot(const CDungeonMap &map, bool back) {
    int replaced = 0, maps = 0, crystals = 0;
    for (const auto &box : map.boxes) {
        if (!box.used) continue;
        // A native locked-door key is placed outside the random loot selector.
        if (box.item_no == selectMapNo + ITEM_BONE_KEY) continue;
        if (!back && box.item_no == ITEM_MAP) { ++maps; continue; }
        if (!back && box.item_no == ITEM_MAGICAL_CRYSTAL) { ++crystals; continue; }
        EXPECT_EQ(box.item_no, box.kind == TREASURE_BOX_LARGE ? ITEM_WEAPON_DAGGER : (back ? ITEM_REPAIR_POWDER : ITEM_BREAD));
        ++replaced;
    }
    EXPECT_GT(replaced, 0);
    if (!back) { EXPECT_EQ(maps, 1); EXPECT_EQ(crystals, 1); }
}

void AuditMonsterTerrain() {
    ASSERT_NO_THROW(chronicle::monsters::ValidateExpandedCollisionResources(*NowDngMap, NowMonstorUnit->collision, 256));
    std::vector<CCPoly> native(65536);
    for (unsigned slot = 0; slot <= NowMonstorUnit->SlotLimit(); ++slot) {
        if (slot < NowMonstorUnit->SlotLimit() && NowMonstorUnit->monster[slot].state != 2) continue;
        float position[4]{};
        if (slot == NowMonstorUnit->SlotLimit()) CharaMain.GetPosition(position);
        else NowMonstorUnit->chara[slot][0].GetPosition(position);
        auto bounded = chronicle::monsters::ExpandedTerrainCollision(*NowDngMap, position, CharaMain.frame->position);
        const auto count = setCollisionData(NowDngMap, native.data(), position, 30, 5);
        ASSERT_EQ(bounded.size(), unsigned(count)) << "monster slot " << slot;
        for (unsigned i = 0; i < bounded.size(); ++i) {
            for (int axis = 0; axis < 3; ++axis) {
                EXPECT_FLOAT_EQ(bounded[i].normal[axis], native[i].normal[axis]);
                for (int v = 0; v < 3; ++v) EXPECT_FLOAT_EQ(bounded[i].vertex[v][axis], native[i].vertex[v][axis]);
            }
            EXPECT_EQ(std::memcmp(&bounded[i].info, &native[i].info, sizeof(CCPolyInfo)), 0);
        }
    }
}

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
    if (audit_lighting_write && queued) {
        const auto saved = nlohmann::json::parse(mods::Scripts().SaveState());
        const auto values = saved.at("mods").at("audit.back").value("values", nlohmann::json::object());
        const int phase = values.value("lighting_phase", 0), hold = values.value("lighting_hold", 0);
        const int sample = phase == 1 && hold == 0 ? 0 : phase == 1 && hold >= 3 ? 1 : phase == 2 && hold >= 3 ? 2 : -1;
        if (sample >= 0 && lighting_red[sample] < 0) {
            std::vector<uint8_t> pixels;
            uint32_t width, height;
            ASSERT_TRUE(gfx::ReadbackFrame(pixels, width, height));
            int red = 0;
            for (std::size_t i = 0; i + 3 < pixels.size(); i += 4)
                if (pixels[i] > 150 && pixels[i+1] < 50 && pixels[i+2] < 70) ++red;
            lighting_red[sample] = red;
            gfx::WritePng((std::filesystem::path(std::getenv("DC_BACK_FLOOR_AUDIT")) / ("lighting-" + std::to_string(sample) + ".png")).string().c_str(), pixels.data(), width, height);
        }
    }
    if (audit_camera_write && queued) {
        const auto saved = nlohmann::json::parse(mods::Scripts().SaveState());
        const auto values = saved.at("mods").at("audit.back").value("values", nlohmann::json::object());
        const int phase = values.value("camera_phase", 0);
        if (phase == 1 && !camera_applied) {
            const auto c = mods::ReadDungeonCamera();
            EXPECT_EQ(c["distance"], 85); EXPECT_EQ(c["height"], 25);
            EXPECT_EQ(c["owners"]["distance"], "audit.back");
            camera_applied = true;
        }
        if (phase == 2 && !camera_released) {
            const auto c = mods::ReadDungeonCamera();
            EXPECT_EQ(c["distance"], values["release_distance"]);
            EXPECT_EQ(c["height"], values["release_height"]);
            EXPECT_EQ(c["owners"]["distance"], "");
            camera_released = true;
        }
    }
    const auto state = mods::ReadBackFloor();
    if (population_injected && gameTask == GAME_TASK_PLAY && NowMonstorUnit->SlotLimit() == unsigned(audit_population)) {
        high_slot_seen |= NowMonstorUnit->monster[audit_population - 1].state == 2;
    }
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
                if (population_injected && GameFrameCount() < population_frame + 180) return;
                if (population_injected && quit_with_population) {
                    EXPECT_TRUE(high_slot_seen);
                    EXPECT_EQ(NowMonstorUnit->SlotLimit(), unsigned(audit_population));
                    arrived = true;
                    if (fail_with_population) throw std::runtime_error("population exit audit");
                    GameRequestStop();
                    return;
                }
                ASSERT_NO_THROW(AuditMonsterTerrain());
                if (audit_chest_loot) { AuditChestLoot(MainDungeonMap, false); AuditChestLoot(UraDungeonMap, true); }
                if (managed_layout && !population_injected) {
                    const auto state = mods::ReadLayout({
                        {"dungeon", selectMapNo}
                    });
                    EXPECT_EQ(state["owner"], "audit.back");
                    EXPECT_EQ(state["last_build"]["state"], expected_expanded ? "applied" : "rejected") << state.dump();
                    if (expected_expanded) {
                        EXPECT_EQ(state["requested"]["chests"], 12);
                        EXPECT_EQ(state["requested"]["monsters"], expected_monsters);
                        EXPECT_EQ(NowMonstorUnit->GetMonstorNum(), expected_monsters);
                        EXPECT_EQ(state["last_build"]["population_front"]["state"], "applied");
                        EXPECT_EQ(state["last_build"]["population_front"]["requested"], expected_monsters);
                        EXPECT_EQ(state["last_build"]["population_front"]["placed"], expected_monsters);
                        const std::array<int, 7> keys{ITEM_DRAN_S_CREST, ITEM_SHINY_STONE, ITEM_HOOK, ITEM_KING_S_SLATE, ITEM_GUN_POWDER, ITEM_CLOCK_HANDS, ITEM_BLACK_KNIGHT_CREST};
                        auto has_carrier = [](int key) {
                            return std::ranges::any_of(NowMonstorUnit->monster, [key](const auto &actor) {
                                return actor.state != -1 && actor.drops_items && actor.drop_item == key;
                            });
                        };
                        EXPECT_TRUE(has_carrier(keys[selectMapNo]));
                        if (selectMapNo == 1) {
                            EXPECT_TRUE(has_carrier(ITEM_RED_BERRY));
                            EXPECT_TRUE(has_carrier(ITEM_POINTY_CHESTNUT));
                        }
                        EXPECT_GE(MainDungeonMap.box_num, 12);
                        EXPECT_GE(UraDungeonMap.box_num, 12);
                        EXPECT_EQ(state["last_build"]["front_chests"], MainDungeonMap.box_num);
                        EXPECT_EQ(state["last_build"]["back_chests"], UraDungeonMap.box_num);
                    }
                    if (!expected_expanded) {
                        EXPECT_FALSE(state["last_build"]["reason"].get<std::string>().empty());
                        EXPECT_EQ(MainDungeonMap.GridExtent(), 20);
                        EXPECT_EQ(UraDungeonMap.GridExtent(), 20);
                    }
                }
                if (audit_population && !population_injected) {
                    chronicle::monsters::PreparePopulationForFloor(*NowMonstorUnit, *NowDngMap, *NowColData, audit_population, 0);
                    const auto requested = NowDngMap->port_monster_count;
                    NowDngMap->port_monster_count = 0;
                    NowMonstorUnit->ArrangementPos(NowDngMap, audit_population, -1, 0);
                    NowDngMap->port_monster_count = requested;
                    if (audit_partial_population) {
                        // All active slots are occupied; native placement cannot
                        // construct another actor even where the terrain fits.
                        NowMonstorUnit->ArrangementPos(NowDngMap, requested, -1, 0);
                        const auto report = mods::ReadLayout({{"dungeon", selectMapNo}})["last_build"]["population_front"];
                        EXPECT_EQ(report["state"], "partial");
                        EXPECT_EQ(report["requested"], requested);
                        EXPECT_EQ(report["placed"], 0);
                        EXPECT_FALSE(report["reason"].get<std::string>().empty());
                    }
                    EXPECT_EQ(NowMonstorUnit->GetMonstorNum(), audit_population);
                    EXPECT_EQ(NowMonstorUnit->SlotLimit(), unsigned(audit_population));
                    EXPECT_GT(NowColData->HitLimit(), 96u);
                    EXPECT_TRUE(chronicle::monsters::ExpandedEffectVoicesActive());
                    EXPECT_GT(chronicle::monsters::SoundSequenceLimit(), 32u);
                    float nearby[4]{};
                    CharaMain.GetPosition(nearby);
                    nearby[0] += 60;
                    NowMonstorUnit->chara[audit_population - 1][0].SetPosition(nearby);
                    population_injected = true;
                    population_frame = GameFrameCount();
                    return;
                }
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
                        if (selectMapNo == 1) {
                            EXPECT_THROW(chronicle::dungeon::PrepareMaps(MainDungeonMap, DngEventMan, *SaveData->GetDngStatus(), {64, 46, 3, 6, 12, 2}), std::runtime_error);
                        }
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
                if (audit_population) EXPECT_TRUE(high_slot_seen);
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
            ASSERT_NO_THROW(AuditMonsterTerrain());
            if (managed_layout && expected_expanded) {
                EXPECT_EQ(NowMonstorUnit->GetMonstorNum(), expected_monsters);
                const auto report = mods::ReadLayout({{"dungeon", selectMapNo}})["last_build"]["population_back"];
                EXPECT_EQ(report["state"], "applied");
                EXPECT_EQ(report["requested"], expected_monsters);
                EXPECT_EQ(report["placed"], expected_monsters);
            }
            if (audit_population) {
                EXPECT_TRUE(population_injected);
                EXPECT_EQ(NowMonstorUnit->SlotLimit(), 16u);
                EXPECT_EQ(NowColData->HitLimit(), 96u);
                EXPECT_EQ(chronicle::monsters::SoundSequenceLimit(), 32u);
                EXPECT_FALSE(chronicle::monsters::ExpandedEffectVoicesActive());
            }
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

void RunBackAudit(int dungeon, bool expanded = false, int rooms = 6, bool managed = false, int monsters = 5, int population = 0, bool quit = false, bool fail = false, bool partial = false, bool chest = false, bool atla = false, bool camera = false, bool camera_write = false, bool lighting = false, bool lighting_write = false, bool retail_sound = false, bool bgm_stop = false) {
    audit_lighting_write = lighting_write; lighting_red = {-1, -1, -1};
    audit_camera_write = camera_write; camera_applied = camera_released = false;
    if (expanded && !std::getenv("DC_EXPANDED_BACK_FLOOR_AUDIT")) {
        GTEST_SKIP() << "Requires explicit expanded-floor audit opt-in";
    }
    const char *data = std::getenv("DC_DATA"), *audit = std::getenv("DC_BACK_FLOOR_AUDIT");
    if (!data || !audit || !*audit) {
        GTEST_SKIP() << "Run alone with DC_DATA and DC_BACK_FLOOR_AUDIT";
    }
    queued = arrived = false;
    expected_expanded = expanded;
    managed_layout = managed;
    audit_population = population;
    quit_with_population = quit;
    fail_with_population = fail;
    audit_partial_population = partial;
    audit_chest_loot = chest;
    population_injected = false;
    high_slot_seen = false;
    population_frame = 0;
    expected_rooms = rooms;
    expected_monsters = monsters;
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
    if (managed) {
        const auto    script_path = root / "mods/audit.back/main.lua";
        std::ifstream input_script(script_path);
        std::string   source((std::istreambuf_iterator<char>(input_script)), {});
        input_script.close();
        const std::string dimensions = expanded ? ",area=46,room_min=3,room_max=6,chests=12,monsters=" + std::to_string(monsters) : ",area=8,room_min=6,room_max=6";
        std::string chest_init;
        if (chest) {
            for (bool back : {false, true}) for (bool weapon : {false, true}) {
                const int item = weapon ? ITEM_WEAPON_DAGGER : (back ? ITEM_REPAIR_POWDER : ITEM_BREAD);
                chest_init += "assert(chronicle.call('dungeon.chest_loot_set',{dungeon=" + std::to_string(dungeon) +
                    ",back=" + (back ? "true" : "false") + ",kind='" + (weapon ? "weapon" : "small") +
                    "',entries={{item=" + std::to_string(item) + ",weight=1}}})) ";
            }
        }
        const std::string init = "return {on_game_enter=function() " + chest_init + "assert(chronicle.call('dungeon.layout_set',{dungeon=" + std::to_string(dungeon) + ",rooms=" + std::to_string(rooms) + dimensions + "})) end,on_tick=";
        source.replace(source.find("return {on_tick="), std::string("return {on_tick=").size(), init);
        std::ofstream(script_path) << source;
    }
    if (camera) {
        const auto script_path = root / "mods/audit.back/main.lua";
        std::ifstream input_script(script_path);
        std::string source((std::istreambuf_iterator<char>(input_script)), {});
        input_script.close();
        const auto where = source.find(" assert(chronicle.call('dungeon.back_floor_enter'))");
        ASSERT_NE(where, std::string::npos);
        source.insert(where, R"(
 local c=assert(chronicle.call('render.camera_get'))
 assert(c.scene=='dungeon' and #c.eye==3 and #c.target==3)
 assert(type(c.following)=='boolean' and type(c.distance)=='number' and type(c.angle)=='number')
 c.eye[1]=999999
 local again=assert(chronicle.call('render.camera_get'))
 assert(again.eye[1]~=999999,'snapshot must be detached')
)");
        std::ofstream(script_path) << source;
    }
    if (lighting) {
        const auto script_path = root / "mods/audit.back/main.lua";
        std::ifstream input_script(script_path);
        std::string source((std::istreambuf_iterator<char>(input_script)), {});
        input_script.close();
        const auto where = source.find(" assert(chronicle.call('dungeon.back_floor_enter'))");
        ASSERT_NE(where, std::string::npos);
        source.insert(where, R"(
 local light=assert(chronicle.call('dungeon.lighting_get'))
 assert(light.side=='front' and light.view_scale>0)
 assert(#light.front.lights==3 and #light.back.lights==3)
 assert(#light.front.lights[1].direction==3 and #light.back.ambient==3)
 local original=light.front.fog.far
 light.front.fog.far=-1
 local again=assert(chronicle.call('dungeon.lighting_get'))
 assert(again.front.fog.far==original,'lighting snapshots must be detached')
)");
        std::ofstream(script_path) << source;
    }
    if (retail_sound) {
        const auto script_path = root / "mods/audit.back/main.lua";
        std::ifstream input_script(script_path);
        std::string source((std::istreambuf_iterator<char>(input_script)), {});
        input_script.close();
        const auto where = source.find(" assert(chronicle.call('dungeon.back_floor_enter'))");
        ASSERT_NE(where, std::string::npos);
        source.insert(where, R"(
 if not chronicle.get('se_committed') then
  assert(chronicle.call('audio.se_play',{se=1,volume=100}))
  chronicle.set('se_committed',true)
  return
 end
 local hold=chronicle.get('se_hold') or 0
 if hold<6 then chronicle.set('se_hold',hold+1);return end
)");
        std::ofstream(script_path) << source;
        const auto manifest_path = root / "mods/audit.back/mod.json";
        std::ifstream input_manifest(manifest_path);
        auto manifest = nlohmann::json::parse(input_manifest);
        input_manifest.close();
        manifest["requires_operations"].push_back("audio.se_play");
        std::ofstream(manifest_path) << manifest.dump();
    }
    if (bgm_stop) {
        const auto script_path = root / "mods/audit.back/main.lua";
        std::ifstream input_script(script_path);
        std::string source((std::istreambuf_iterator<char>(input_script)), {});
        input_script.close();
        const auto where = source.find(" assert(chronicle.call('dungeon.back_floor_enter'))");
        ASSERT_NE(where, std::string::npos);
        source.insert(where, R"(
 local music=assert(chronicle.call('audio.bgm_state'))
 if not chronicle.get('bgm_stopped') then
  assert(music.playing and music.bank_loaded and not music.loading and not music.disabled)
  assert(#music.tracks>0 and music.tracks[1].ready)
  chronicle.set('bgm_set',music.set)
  assert(chronicle.call('audio.bgm_stop'))
  chronicle.set('bgm_stopped',true)
  return
 end
 assert(not music.playing and music.set==chronicle.get('bgm_set'))
 chronicle.set('bgm_verified',true)
)");
        std::ofstream(script_path) << source;
    }
    if (lighting_write) {
        const auto script_path = root / "mods/audit.back/main.lua";
        std::ifstream input_script(script_path);
        std::string source((std::istreambuf_iterator<char>(input_script)), {});
        input_script.close();
        const auto where = source.find(" assert(chronicle.call('dungeon.back_floor_enter'))");
        ASSERT_NE(where, std::string::npos);
        source.insert(where, R"(
 local light=assert(chronicle.call('dungeon.lighting_get'))
 if not light.writable then return end
 local phase=chronicle.get('lighting_phase') or 0
 if phase==0 then
  local warmup=chronicle.get('lighting_warmup') or 0
  chronicle.set('lighting_warmup',warmup+1)
  if warmup<90 then return end
  chronicle.set('lighting_far',light.front.fog.far)
  chronicle.set('lighting_scale',light.view_scale)
  assert(chronicle.call('dungeon.lighting_set',{side='both',fog={near=1,far=2,min=0,max=0,color={200,10,20}},background={200,10,20},view_scale=3}))
  chronicle.set('lighting_phase',1); chronicle.set('lighting_hold',0)
  return
 end
 local hold=chronicle.get('lighting_hold') or 0
 chronicle.set('lighting_hold',hold+1)
 if hold<4 then return end
 if phase==1 then
  assert(light.front.fog.far==2 and light.back.fog.far==2 and light.view_scale==3)
  assert(light.owners['/front/fog']=='audit.back')
  assert(chronicle.call('dungeon.lighting_reset',{}))
  chronicle.set('lighting_phase',2); chronicle.set('lighting_hold',0)
  return
 end
 assert(light.front.fog.far==chronicle.get('lighting_far') and light.view_scale==chronicle.get('lighting_scale'))
 assert(next(light.owners)==nil)
)");
        std::ofstream(script_path) << source;
    }
    if (camera_write) {
        const auto script_path = root / "mods/audit.back/main.lua";
        std::ifstream input_script(script_path);
        std::string source((std::istreambuf_iterator<char>(input_script)), {});
        input_script.close();
        const auto where = source.find(" assert(chronicle.call('dungeon.back_floor_enter'))");
        ASSERT_NE(where, std::string::npos);
        source.insert(where, R"(
 local c=assert(chronicle.call('render.camera_get'))
 if not c.writable then return end
 local phase=chronicle.get('camera_phase') or 0
 if phase==0 then
  chronicle.set('camera_distance',c.distance)
  chronicle.set('camera_height',c.height)
  assert(chronicle.call('render.camera_set',{distance=85,height=25}))
  chronicle.set('camera_phase',1)
  return
 elseif phase==1 then
  chronicle.set('release_distance',c.distance==85 and chronicle.get('camera_distance') or c.distance)
  chronicle.set('release_height',c.height==25 and chronicle.get('camera_height') or c.height)
  assert(c.owners.distance=='audit.back' and c.owners.height=='audit.back')
  assert(chronicle.call('render.camera_reset',{}))
  chronicle.set('camera_phase',2)
  return
 end
 assert(c.owners.distance=='' and c.owners.height=='')
)");
        std::ofstream(script_path) << source;
    }
    if (atla) {
        const auto script_path = root / "mods/audit.back/main.lua";
        std::ifstream input_script(script_path);
        std::string source((std::istreambuf_iterator<char>(input_script)), {});
        input_script.close();
        const std::string target = " assert(chronicle.call('dungeon.back_floor_enter'))";
        const auto where = source.find(target);
        ASSERT_NE(where, std::string::npos);
        source.insert(where, R"(
 if not chronicle.get('atla_done') then
  local before=assert(chronicle.call('georama.atla_list'))
  assert(before.count>0,'fixture needs an Atla sphere')
  chronicle.set('atla_count',before.count)
  chronicle.set('atla_slot',before.spheres[1].slot)
  assert(chronicle.call('georama.atla_collect',{slot=before.spheres[1].slot}))
  chronicle.set('atla_done',true)
  return
 end
 local after=assert(chronicle.call('georama.atla_list'))
 assert(after.count==chronicle.get('atla_count')-1,'sphere was not retired')
 for _,sphere in ipairs(after.spheres) do assert(sphere.slot~=chronicle.get('atla_slot')) end
)");
        std::ofstream(script_path) << source;
        const auto manifest_path = root / "mods/audit.back/mod.json";
        std::ifstream manifest_input(manifest_path);
        auto manifest = nlohmann::json::parse(manifest_input);
        manifest_input.close();
        manifest["requires_operations"].push_back("georama.atla_list");
        manifest["requires_operations"].push_back("georama.atla_collect");
        std::ofstream(manifest_path) << manifest.dump();
    }
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
    if (expanded && !managed) {
        layout = std::make_unique<chronicle::dungeon::BuildScope>(chronicle::dungeon::BuildLayout{rooms, 46, 3, rooms > 6 ? 6 : 4});
    }
    int result = 0;
    bool caught = false;
    try {
        result = RunGame(0, nullptr);
    } catch (const std::runtime_error &error) {
        if (!fail_with_population) throw;
        EXPECT_STREQ(error.what(), "population exit audit");
        caught = true;
    }
    EXPECT_EQ(caught, fail_with_population);
    layout.reset();
    ClockRemovePumpHook(BackPump);
    InputShutdown();
    EXPECT_EQ(result, 0);
    if (retail_sound) {
        const auto state = nlohmann::json::parse(mods::Scripts().SaveState());
        const auto values = state.at("mods").at("audit.back").at("values");
        EXPECT_EQ(values["se_committed"], true);
        EXPECT_EQ(values["se_hold"], 6);
    }
    if (bgm_stop) {
        const auto state = nlohmann::json::parse(mods::Scripts().SaveState());
        EXPECT_EQ(state.at("mods").at("audit.back").at("values").at("bgm_verified"), true);
    }
    if (camera_write) { EXPECT_TRUE(camera_applied); EXPECT_TRUE(camera_released); }
    if (lighting_write) {
        EXPECT_GE(lighting_red[0], 0); EXPECT_GE(lighting_red[2], 0);
        EXPECT_GT(lighting_red[1], lighting_red[0] + 1000);
        EXPECT_GT(lighting_red[1], lighting_red[2] + 1000);
    }
    EXPECT_EQ(queued, !quit_with_population) << last_state;
    EXPECT_TRUE(arrived) << last_state << " task=" << gameTask << " longest_play=" << maximum_stable;
    if (quit_with_population || expected_monsters > 16) {
        EXPECT_EQ(NowMonstorUnit->SlotLimit(), 16u);
        EXPECT_EQ(NowColData->HitLimit(), 96u);
        EXPECT_EQ(chronicle::monsters::SoundSequenceLimit(), 32u);
        EXPECT_FALSE(chronicle::monsters::ExpandedEffectVoicesActive());
        for (int i = 16; i < std::max(audit_population, expected_monsters); ++i) {
            EXPECT_EQ(NowMonstorUnit->monster[i].state, -1);
            EXPECT_EQ(NowMonstorUnit->script[i], nullptr);
        }
    }
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

TEST(ModBackFloorRetail, ManagedCaveLayout) { RunBackAudit(0, true, 64, true); }
TEST(ModBackFloorRetail, ManagedWiseOwlLayout) { RunBackAudit(1, true, 64, true); }
TEST(ModBackFloorRetail, ManagedShipwreckLayout) { RunBackAudit(2, true, 64, true); }
TEST(ModBackFloorRetail, ManagedTempleLayout) { RunBackAudit(3, true, 64, true); }
TEST(ModBackFloorRetail, ManagedMoonSeaLayout) { RunBackAudit(4, true, 64, true); }
TEST(ModBackFloorRetail, ManagedGalleryLayout) { RunBackAudit(5, true, 64, true); }
TEST(ModBackFloorRetail, ManagedDemonShaftLayout) { RunBackAudit(6, true, 64, true); }
TEST(ModBackFloorRetail, RejectedManagedLayoutUsesRetail) { RunBackAudit(0, false, 2, true); }
TEST(ModBackFloorRetail, MinimumCavePopulation) { RunBackAudit(0, true, 64, true, 2); }
TEST(ModBackFloorRetail, MinimumForestPopulation) { RunBackAudit(1, true, 64, true, 3); }
TEST(ModBackFloorRetail, ExpandedPopulationReturnsToRetailLimits) { RunBackAudit(0, true, 64, true, 5, 32); }
TEST(ModBackFloorRetail, HighPopulationReturnsToRetailLimits) { RunBackAudit(0, true, 64, true, 5, 129); }
TEST(ModBackFloorRetail, MaximumPopulationReturnsToRetailLimits) { RunBackAudit(0, true, 64, true, 5, 256); }
TEST(ModBackFloorRetail, MaximumForestPopulation) { RunBackAudit(1, true, 64, true, 5, 256); }
TEST(ModBackFloorRetail, MaximumShipwreckPopulation) { RunBackAudit(2, true, 64, true, 5, 256); }
TEST(ModBackFloorRetail, MaximumTemplePopulation) { RunBackAudit(3, true, 64, true, 5, 256); }
TEST(ModBackFloorRetail, MaximumMoonSeaPopulation) { RunBackAudit(4, true, 64, true, 5, 256); }
TEST(ModBackFloorRetail, MaximumGalleryPopulation) { RunBackAudit(5, true, 64, true, 5, 256); }
TEST(ModBackFloorRetail, MaximumDemonShaftPopulation) { RunBackAudit(6, true, 64, true, 5, 256); }
TEST(ModBackFloorRetail, QuitWithMaximumPopulation) { RunBackAudit(0, true, 64, true, 5, 256, true); }
TEST(ModBackFloorRetail, ExceptionWithMaximumPopulation) { RunBackAudit(0, true, 64, true, 5, 256, true, true); }
TEST(ModBackFloorRetail, PartialPopulationIsReported) { RunBackAudit(0, true, 64, true, 5, 32, false, false, true); }
TEST(ModBackFloorRetail, PublicMaximumCavePopulation) { RunBackAudit(0, true, 64, true, 256); }
TEST(ModBackFloorRetail, PublicMaximumForestPopulation) { RunBackAudit(1, true, 64, true, 256); }
TEST(ModBackFloorRetail, PublicMaximumShipwreckPopulation) { RunBackAudit(2, true, 64, true, 256); }
TEST(ModBackFloorRetail, PublicMaximumTemplePopulation) { RunBackAudit(3, true, 64, true, 256); }
TEST(ModBackFloorRetail, PublicMaximumMoonSeaPopulation) { RunBackAudit(4, true, 64, true, 256); }
TEST(ModBackFloorRetail, PublicMaximumGalleryPopulation) { RunBackAudit(5, true, 64, true, 256); }
TEST(ModBackFloorRetail, PublicMaximumDemonShaftPopulation) { RunBackAudit(6, true, 64, true, 256); }
TEST(ModBackFloorRetail, OwnedChestLootPreservesMapAndCrystal) { RunBackAudit(0, true, 64, true, 5, 0, false, false, false, true); }
TEST(ModBackFloorRetail, OwnedChestLootForest) { RunBackAudit(1, true, 64, true, 5, 0, false, false, false, true); }
TEST(ModBackFloorRetail, OwnedChestLootShipwreck) { RunBackAudit(2, true, 64, true, 5, 0, false, false, false, true); }
TEST(ModBackFloorRetail, OwnedChestLootTemple) { RunBackAudit(3, true, 64, true, 5, 0, false, false, false, true); }
TEST(ModBackFloorRetail, OwnedChestLootMoonSea) { RunBackAudit(4, true, 64, true, 5, 0, false, false, false, true); }
TEST(ModBackFloorRetail, OwnedChestLootGallery) { RunBackAudit(5, true, 64, true, 5, 0, false, false, false, true); }
TEST(ModBackFloorRetail, OwnedChestLootDemonShaft) { RunBackAudit(6, true, 64, true, 5, 0, false, false, false, true); }
TEST(ModBackFloorRetail, AtlaCollectionThroughSharedActionPreservesBackTransition) { RunBackAudit(0, true, 64, true, 5, 0, false, false, false, false, true); }
TEST(ModBackFloorRetail, CameraSnapshotThroughSharedActionPreservesBackTransition) { RunBackAudit(0, false, 6, false, 5, 0, false, false, false, false, false, true); }
TEST(ModBackFloorRetail, CameraOrbitThroughSharedActionRestoresBeforeBackTransition) { RunBackAudit(0, false, 6, false, 5, 0, false, false, false, false, false, false, true); }
TEST(ModBackFloorRetail, LightingSnapshotThroughSharedActionPreservesBackTransition) { RunBackAudit(0, false, 6, false, 5, 0, false, false, false, false, false, false, false, true); }
TEST(ModBackFloorRetail, LightingWriteResetRendersAndPreservesBackTransition) { RunBackAudit(0, false, 6, false, 5, 0, false, false, false, false, false, false, false, false, true); }
} // namespace

TEST(ModBackFloorRetail, RetailSoundThroughSharedActionPreservesBackTransition) { RunBackAudit(0, false, 6, false, 5, 0, false, false, false, false, false, false, false, false, false, true); }

TEST(ModBackFloorRetail, BgmStopThroughSharedActionPreservesBackTransition) { RunBackAudit(0, false, 6, false, 5, 0, false, false, false, false, false, false, false, false, false, false, true); }
