#include <gtest/gtest.h>
#include "mods/actor_spawn.hpp"
#include "monstorunit.hpp"
#include "dataalloc.hpp"
#include "frame.hpp"
#include <limits>
#include <memory>

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
