#include <gtest/gtest.h>
#include "mods/model_identity.hpp"
#include "mods/actor_spawn.hpp"
#include "mods/entity_tracking.hpp"
#include "monstorunit.hpp"
#include "frame.hpp"
#include <memory>
#include <cstring>
namespace {
class ModModelIdentity : public testing::Test {
protected:
    std::unique_ptr<CMonstorUnit> unit = std::make_unique<CMonstorUnit>();
    CFrame first, second; char script[2]{};
    void SetUp() override {
        unit->CleanViewMonstor(0); unit->model_count = 2;
        for (int i = 0; i < 2; ++i) {
            unit->model[i] = {}; unit->model[i].max_hp = 10; unit->model[i].collision_radius = 13;
            unit->script_data[i] = script; unit->base_chara[i][0].frame = &first;
        }
    }
    void TearDown() override { mods::ClearNativeModelIdentities(unit.get()); }
};
}
TEST_F(ModModelIdentity, ExactLoaderIdsDistinguishIdenticalDescriptorsWithoutEngineWrites) {
    EXPECT_EQ(mods::NativeMonsterModelId(unit.get(), 0), -1);
    std::vector<unsigned char> before(sizeof(*unit)); std::memcpy(before.data(), unit.get(), before.size());
    srand(19); const int expected = rand(); srand(19);
    mods::RecordNativeModelLoad(unit.get(), 0, 3);
    mods::RecordNativeModelLoad(unit.get(), 1, 27);
    EXPECT_EQ(mods::NativeMonsterModelId(unit.get(), 0), 3);
    EXPECT_EQ(mods::NativeMonsterModelId(unit.get(), 1), 27);
    EXPECT_EQ(std::memcmp(before.data(), static_cast<const void *>(unit.get()), before.size()), 0);
    EXPECT_EQ(rand(), expected); EXPECT_EQ(mods::TrackedEntities(unit.get()), nullptr);
    const auto state = mods::ReadLoadedActorModels(unit.get());
    EXPECT_EQ(state["models"][0]["native_model_id"], 3); EXPECT_EQ(state["models"][1]["native_model_id"], 27);
    // Owned loot/stat edits to the source description must not change the
    // identity recorded at load time or force a guessed descriptor match.
    unit->model[0].max_hp = 99;
    EXPECT_EQ(mods::NativeMonsterModelId(unit.get(), 0), 3);
}
TEST_F(ModModelIdentity, ReloadInvalidatesBeforeCompletionAndChangedAssetsFailClosed) {
    mods::RecordNativeModelLoad(unit.get(), 0, 3); mods::RecordNativeModelLoad(unit.get(), 1, 27);
    mods::BeginNativeModelLoad(unit.get(), 0);
    EXPECT_EQ(mods::NativeMonsterModelId(unit.get(), 0), -1);
    EXPECT_EQ(mods::NativeMonsterModelId(unit.get(), 1), 27);
    mods::RecordNativeModelLoad(unit.get(), 0, 8);
    EXPECT_EQ(mods::NativeMonsterModelId(unit.get(), 0), 8);
    unit->base_chara[0][0].frame = &second;
    EXPECT_EQ(mods::NativeMonsterModelId(unit.get(), 0), -1);
    unit->base_chara[0][0].frame = &first; unit->script_data[0] = script + 1;
    EXPECT_EQ(mods::NativeMonsterModelId(unit.get(), 0), -1);
}
TEST_F(ModModelIdentity, ActorOnlyClearPreservesModelsButDiscardedModelTableExpiresThem) {
    mods::RecordNativeModelLoad(unit.get(), 0, 3);
    unit->monster[0].state = 1; unit->monster[0].base_model = 0;
    EXPECT_EQ(mods::NativeActorModelId(unit.get(), 0), 3);
    EXPECT_EQ(mods::NativeActorModelId(unit.get(), unit->SlotLimit()), -1);
    unit->CleanViewMonstor(0);
    EXPECT_EQ(mods::NativeActorModelId(unit.get(), 0), -1);
    EXPECT_EQ(mods::NativeMonsterModelId(unit.get(), 0), 3);
    unit->model_count = 0; unit->CleanViewMonstor(0); unit->model_count = 2;
    EXPECT_EQ(mods::NativeMonsterModelId(unit.get(), 0), -1);
}
TEST_F(ModModelIdentity, InvalidLoadNotificationsCannotCreateIdentitiesOrEvictAnotherSlot) {
    mods::RecordNativeModelLoad(nullptr, 0, 0);
    mods::RecordNativeModelLoad(unit.get(), -1, 0); mods::RecordNativeModelLoad(unit.get(), 2, 0);
    mods::RecordNativeModelLoad(unit.get(), 0, 167); mods::RecordNativeModelLoad(unit.get(), 0, -1);
    EXPECT_EQ(mods::NativeMonsterModelId(unit.get(), 0), -1);
    mods::RecordNativeModelLoad(unit.get(), 0, 3);
    mods::BeginNativeModelLoad(unit.get(), 9);
    EXPECT_EQ(mods::NativeMonsterModelId(unit.get(), 0), 3);
    unit->model_count = 10; EXPECT_EQ(mods::NativeMonsterModelId(unit.get(), 0), -1);
    mods::EntityUnitCleared(unit.get()); unit->model_count = 2;
    EXPECT_EQ(mods::NativeMonsterModelId(unit.get(), 0), -1);
}
