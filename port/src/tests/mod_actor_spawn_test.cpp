#include <gtest/gtest.h>
#include "mods/actor_spawn.hpp"
#include "monstorunit.hpp"
#include "dataalloc.hpp"
#include "frame.hpp"
#include <limits>
#include <memory>
#include <cstring>

TEST(ModActorSpawn, RejectsUnavailableModelsScriptsFramesAndMimics) {
    auto unit = std::make_unique<CMonstorUnit>();
    std::string error;
    EXPECT_FALSE(mods::PlanLoadedActorSpawn(nullptr, 0, {}, error));
    unit->model_count = 0;
    EXPECT_FALSE(mods::PlanLoadedActorSpawn(unit.get(), 0, {}, error));
    unit->model_count = 10;
    EXPECT_FALSE(mods::PlanLoadedActorSpawn(unit.get(), 0, {}, error));
    unit->model_count = 1;
    unit->model[0].attachment_kind = 0; unit->model[0].kind = 0;
    unit->script_data[0] = nullptr;
    EXPECT_FALSE(mods::PlanLoadedActorSpawn(unit.get(), 0, {}, error));
    EXPECT_EQ(error, "loaded monster script unavailable");
    char script = 0; unit->script_data[0] = &script; unit->base_chara[0][0].frame = nullptr;
    EXPECT_FALSE(mods::PlanLoadedActorSpawn(unit.get(), 0, {}, error));
    EXPECT_EQ(error, "loaded monster model frame unavailable");
    unit->model[0].kind = MONSTER_KIND_MIMIC_SMALL;
    EXPECT_FALSE(mods::PlanLoadedActorSpawn(unit.get(), 0, {}, error));
    EXPECT_NE(error.find("mimic"), std::string::npos);
}
TEST(ModActorSpawn, UsesExactFirstFreeSlotWithoutMutatingNativeStateOrRandomStream) {
    auto unit = std::make_unique<CMonstorUnit>();
    unit->CleanViewMonstor(0); unit->model_count = 1;
    unit->model[0].attachment_kind = 0; unit->model[0].kind = 0;
    for (int part = 1; part < 4; ++part) unit->model[0].model_name[part][0] = 0;
    CFrame frame; unit->base_chara[0][0].frame = &frame;
    char script = 0; unit->script_data[0] = &script;
    CDataAlloc2<1> arena(448); alignas(16) unsigned char storage[448 * 16]{};
    arena.base = storage; arena.used = 123;
    unit->script[0] = nullptr; unit->script[1] = &arena;
    std::string error;
    EXPECT_FALSE(mods::PlanLoadedActorSpawn(unit.get(), 0, {}, error));
    EXPECT_NE(error.find("first free"), std::string::npos);
    unit->monster[0].state = 1;
    srand(73); const auto expected = rand(); srand(73);
    auto plan = mods::PlanLoadedActorSpawn(unit.get(), 0, {1, 2, 3}, error);
    ASSERT_TRUE(plan) << error;
    EXPECT_EQ(plan->slot, 1); EXPECT_EQ(plan->model, 0);
    EXPECT_EQ(plan->position, (std::array<float, 3>{1, 2, 3}));
    EXPECT_EQ(arena.used, 123); EXPECT_EQ(unit->monster[1].state, -1); EXPECT_EQ(rand(), expected);
    arena.limit = 447;
    EXPECT_FALSE(mods::PlanLoadedActorSpawn(unit.get(), 0, {}, error));
    arena.limit = 448;
    EXPECT_FALSE(mods::PlanLoadedActorSpawn(unit.get(), 0, {std::numeric_limits<float>::quiet_NaN(), 0, 0}, error));
    unit->model[0].model_name[1][0] = 'x'; unit->base_chara[0][1].frame = nullptr;
    EXPECT_FALSE(mods::PlanLoadedActorSpawn(unit.get(), 0, {}, error));
    EXPECT_NE(error.find("attachment"), std::string::npos);
    unit->model[0].model_name[1][0] = 0;
    for (int i = 0; i < unit->SlotLimit(); ++i) unit->monster[i].state = 1;
    EXPECT_FALSE(mods::PlanLoadedActorSpawn(unit.get(), 0, {}, error));
    EXPECT_NE(error.find("no free slot"), std::string::npos);
}

#include "mods/actor_cleanup.hpp"
#include "mods/entity_tracking.hpp"
#include "dungeonmap.hpp"
#include "dungeonparts.hpp"
#include "dun/gameloop.hpp"
#include "userstatus.hpp"

TEST(ModActorSpawn, NativeCreationOwnershipRetirementAndReuseUseFreshIdentities) {
    auto unit = std::make_unique<CMonstorUnit>(); unit->CleanViewMonstor(0);
    auto map = std::make_unique<CDungeonMap>(); map->port_grid_extent = 20;
    for (auto &cell : map->cells) cell.parts_no = MAP_PARTS_NONE;
    map->cells[0].parts_no = 0; map->cells[0].direction = 0;
    map->box_num = map->atra_num = 0;
    for (auto &event : map->events) event.kind = DNG_EVENT_NONE;
    for (auto &trap : map->trap_circle) trap.state = 0;
    ITEM_FREE_AREA areas[2]{}; areas[0].parts_no = 0; areas[0].rect_num = 1;
    areas[0].rect[0] = {-10, 0, -10, 10, 0, 10}; areas[1].parts_no = -1;
    CFrame frame; unit->base_chara[0][0].frame = &frame;
    unit->base_chara[0][0].shadow_frame = nullptr;
    unit->model_count = 1; unit->model[0] = {};
    unit->model[0].max_hp = 25; unit->model[0].collision_radius = 13;
    // A valid compiled header with no initialization program. Native setup
    // still loads the interpreter, copies the model and initializes the actor.
    RS_PROG_HEADER program{}; unit->script_data[0] = reinterpret_cast<char *>(&program);
    CDataAlloc2<1> arena(448); alignas(16) unsigned char storage[448 * 16]{};
    arena.base = storage; unit->script[0] = &arena;
    struct Restore {
        CUserStatus *previous = UserStatus;
        CMonstorUnit *previous_unit = NowMonstorUnit;
        CDungeonMap *previous_map = NowDngMap;
        ~Restore() { UserStatus = previous; NowMonstorUnit = previous_unit; NowDngMap = previous_map; mods::StopTrackingEntities(); }
    } restore;
    CUserStatus status{}; UserStatus = &status; NowMonstorUnit = unit.get(); NowDngMap = map.get();
    mods::StopTrackingEntities();
    mods::ActorOwners owners; std::string error;
    auto spawn = [&] { return mods::CreateLoadedActor(*unit, *map, areas, owners, "alpha", "guard", 0,
                                                     {0, 0, 0}, {1000, 0, 1000}, 13, error); };
    EXPECT_FALSE(spawn()); // Rejection does not activate tracking.
    EXPECT_EQ(unit->alive_count, 0); EXPECT_EQ(unit->monster[0].state, -1);
    auto &handles = mods::TrackEntities(*unit);
    NowMonstorUnit = nullptr; EXPECT_FALSE(spawn()); NowMonstorUnit = unit.get();
    NowDngMap = nullptr; EXPECT_FALSE(spawn()); NowDngMap = map.get();
    EXPECT_EQ(handles.Get(0), 0); EXPECT_EQ(unit->alive_count, 0);
    // Failed geometry and ownership checks must leave the native slot untouched.
    map->cells[0].parts_no = MAP_PARTS_NONE;
    EXPECT_FALSE(spawn()); EXPECT_EQ(handles.Get(0), 0); EXPECT_EQ(owners.Size(), 0);
    map->cells[0].parts_no = 0;
    const auto native_before = unit->monster[0];
    EXPECT_FALSE(mods::CreateLoadedActor(*unit, *map, areas, owners, "", "guard", 0,
                                       {0, 0, 0}, {1000, 0, 1000}, 13, error));
    EXPECT_EQ(handles.Get(0), 0); EXPECT_EQ(owners.Size(), 0);
    EXPECT_EQ(std::memcmp(&unit->monster[0], &native_before, sizeof(native_before)), 0);
    unit->model[0].max_hp = 0; EXPECT_FALSE(spawn()); unit->model[0].max_hp = 25;
    EXPECT_EQ(unit->alive_count, 0); EXPECT_EQ(handles.Get(0), 0);
    srand(19); const int expected = rand(); srand(19);
    const auto first = spawn(); ASSERT_TRUE(first) << error;
    EXPECT_EQ(rand(), expected); EXPECT_EQ(unit->alive_count, 1);
    EXPECT_EQ(unit->monster[0].hp, 25); EXPECT_EQ(unit->monster[0].state, 1);
    EXPECT_EQ(unit->monster[0].event_flag2, -1); EXPECT_EQ(arena.used, 448);
    ASSERT_TRUE(owners.Find(handles, *first)); EXPECT_EQ(owners.Find(handles, *first)->owner, "alpha");
    EXPECT_TRUE(mods::RetireOwnedActor(unit.get(), owners, *first, "alpha", nullptr, nullptr, -1, -1, -1, error)) << error;
    EXPECT_EQ(unit->alive_count, 0); EXPECT_FALSE(handles.Resolve(*first));
    const auto second = spawn(); ASSERT_TRUE(second) << error;
    EXPECT_NE(*second, *first); EXPECT_EQ(handles.Resolve(*second), 0);
    EXPECT_EQ(owners.Size(), 1); EXPECT_EQ(unit->alive_count, 1);
    EXPECT_FALSE(owners.Find(handles, *first));
    ASSERT_TRUE(mods::RetireOwnedActor(unit.get(), owners, *second, "alpha", nullptr, nullptr, -1, -1, -1, error)) << error;
    unit->script[0] = nullptr; unit->base_chara[0][0].frame = nullptr;
}

#include "mods/api.hpp"
#include "mods/entities.hpp"

TEST(ModActorSpawn, ModelQueryReportsReadinessWithoutTrackingOrNativeWrites) {
    auto unit = std::make_unique<CMonstorUnit>(); unit->CleanViewMonstor(0);
    unit->model_count = 2;
    for (int i = 0; i < 2; ++i) {
        unit->model[i] = {}; unit->model[i].max_hp = 12; unit->model[i].collision_radius = 13;
        unit->script_data[i] = nullptr;
    }
    CFrame frame; RS_PROG_HEADER program{};
    unit->base_chara[0][0].frame = &frame; unit->script_data[0] = reinterpret_cast<char *>(&program);
    CDataAlloc2<1> arena(448); alignas(16) unsigned char storage[448 * 16]{};
    arena.base = storage; unit->script[0] = &arena;
    mods::StopTrackingEntities();
    std::vector<unsigned char> before(sizeof(*unit)); std::memcpy(before.data(), unit.get(), before.size());
    srand(71); const int expected = rand(); srand(71);
    auto result = mods::ReadEntities(unit.get(), "entities.models", nlohmann::json::object());
    EXPECT_TRUE(result["available"]); EXPECT_EQ(result["free_slots"], 16); EXPECT_EQ(result["models"].size(), 2);
    EXPECT_TRUE(result["models"][0]["resources_ready"]);
    EXPECT_FALSE(result["models"][1]["resources_ready"]);
    EXPECT_EQ(result["models"][1]["reason"], "loaded monster script unavailable");
    EXPECT_EQ(mods::TrackedEntities(unit.get()), nullptr);
    EXPECT_EQ(std::memcmp(before.data(), static_cast<const void *>(unit.get()), before.size()), 0);
    EXPECT_EQ(rand(), expected);
    result["models"][0]["max_hp"] = 99;
    EXPECT_EQ(mods::ReadLoadedActorModels(unit.get())["models"][0]["max_hp"], 12);
    unit->model[0].collision_radius = std::numeric_limits<float>::quiet_NaN();
    const auto invalid = mods::ReadLoadedActorModels(unit.get())["models"][0];
    EXPECT_FALSE(invalid["resources_ready"]); EXPECT_TRUE(invalid["radius"].is_null());
    unit->model_count = 10;
    EXPECT_FALSE(mods::ReadLoadedActorModels(unit.get())["available"]);
    // Missing native context must not deactivate an unrelated live registry.
    auto &handles = mods::TrackEntities(*unit);
    EXPECT_FALSE(mods::ReadEntities(nullptr, "entities.models", nlohmann::json::object())["available"]);
    EXPECT_TRUE(handles.Active());
    EXPECT_TRUE(mods::ValidateOperation("entities.models", nlohmann::json::object()).empty());
    EXPECT_FALSE(mods::ValidateOperation("entities.models", {{"extra", 1}}).empty());
    const auto *op = mods::FindOperation("entities.models"); ASSERT_NE(op, nullptr);
    EXPECT_FALSE(op->writes); EXPECT_EQ(op->permission, "game.read");
    mods::StopTrackingEntities(); unit->script[0] = nullptr; unit->base_chara[0][0].frame = nullptr;
}
