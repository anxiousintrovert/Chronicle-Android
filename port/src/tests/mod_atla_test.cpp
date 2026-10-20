#include <gtest/gtest.h>
#include <cstring>
#include "mods/atla.hpp"
#include "mods/api.hpp"
#include "dungeonmap.hpp"
#include "savedata.hpp"
#include "editatra.hpp"

namespace {
class ModAtla : public testing::Test {
protected:
    std::unique_ptr<CSaveData> save = std::make_unique<CSaveData>();
    std::unique_ptr<CDungeonMap> map = std::make_unique<CDungeonMap>();
    std::string error;
    void SetUp() override {
        map->map_type = 1; map->atra_num = 1;
        for (auto &ball : map->atra) ball = {};
        for (auto &event : map->events) event.kind = DNG_EVENT_NONE;
        map->atra[0].used = 1; map->atra[0].atra_no = 0;
        map->events[0].kind = DNG_EVENT_ATRA; map->events[0].index = 0;
        auto &status = *save->GetDngStatus();
        status.cur_chara = 0;
        for (auto &entry : status.atra_grid[0][0]) entry = -1;
        status.atra_grid[0][0][0] = 0;
        status.atra_registry[0][0] = {0, 1, 1};
        save->GetEditPartsInfo(0, 0)->obtained = 0;
    }
};
TEST_F(ModAtla, DetachedReadPreservesSaveMapAndRng) {
    const auto before = std::make_unique<CSaveData>(*save);
    const auto geometry = std::make_unique<CDungeonMap>(*map);
    srand(730); const int next = rand(); srand(730);
    auto result = mods::ReadAtlaSnapshot(*save, *map, 0, 0, false);
    EXPECT_EQ(result["count"], 1);
    EXPECT_EQ(result["spheres"][0]["contents"]["kind"], "part");
    result["spheres"][0]["contents"]["obtained"] = true;
    EXPECT_EQ(std::memcmp(before.get(), save.get(), sizeof(CSaveData)), 0);
    EXPECT_EQ(std::memcmp(geometry.get(), map.get(), sizeof(CDungeonMap)), 0);
    EXPECT_EQ(rand(), next);
    map->atra_num = 0;
    EXPECT_EQ(mods::ReadAtlaSnapshot(*save, *map, 6, 0, false)["count"], 0);
    map->atra_num = 1;
    EXPECT_THROW(mods::ReadAtlaSnapshot(*save, *map, 6, 0, false), std::runtime_error);
    map->atra[0].atra_no = 100;
    EXPECT_THROW(mods::ReadAtlaSnapshot(*save, *map, 0, 0, false), std::runtime_error);
}
TEST_F(ModAtla, NativePartGrantAndRegistryRetirementPublishTogether) {
    auto expected = std::make_unique<CSaveData>(*save);
    ASSERT_EQ(expected->AtraPartsGet(0, 0), 1);
    expected->GetDngStatus()->GetAtraData(0, 0, 0);
    ASSERT_TRUE(mods::CollectAtla(*save, *map, 0, 0, 0, error)) << error;
    EXPECT_EQ(std::memcmp(expected.get(), save.get(), sizeof(CSaveData)), 0);
    EXPECT_EQ(map->atra[0].used, 0);
    EXPECT_EQ(map->events[0].kind, DNG_EVENT_NONE);
    EXPECT_EQ(save->GetDngStatus()->atra_grid[0][0][0], -3);
    EXPECT_EQ(save->GetDngStatus()->atra_registry[0][0].id, -1);
    EXPECT_FALSE(mods::CollectAtla(*save, *map, 0, 0, 0, error));
    EXPECT_EQ(std::memcmp(expected.get(), save.get(), sizeof(CSaveData)), 0);
}
TEST_F(ModAtla, RejectionsPreserveBothSidesAndRejectMixedCommands) {
    save->GetDngStatus()->cur_chara = 1;
    const auto before = std::make_unique<CSaveData>(*save);
    const auto geometry = std::make_unique<CDungeonMap>(*map);
    EXPECT_FALSE(mods::CollectAtla(*save, *map, 0, 0, 0, error));
    EXPECT_EQ(std::memcmp(before.get(), save.get(), sizeof(CSaveData)), 0);
    EXPECT_EQ(std::memcmp(geometry.get(), map.get(), sizeof(CDungeonMap)), 0);
    save->GetDngStatus()->cur_chara = 0;
    save->GetDngStatus()->atra_grid[0][0][0] = -1;
    EXPECT_FALSE(mods::CollectAtla(*save, *map, 0, 0, 0, error));
    EXPECT_EQ(map->atra[0].used, 1);
    EXPECT_EQ(save->GetEditPartsInfo(0, 0)->obtained, 0);
    EXPECT_FALSE(mods::ApplyAtla({{"georama.atla_collect", {{"slot", 0}}, "test.a"}, {"economy.grant", {{"amount", 1}}, "test.a"}}, 0, error));
    EXPECT_FALSE(mods::ValidateOperation("georama.atla_collect", {{"slot", 8}}).empty());
    EXPECT_FALSE(mods::ValidateOperation("georama.atla_list", {{"slot", 0}}).empty());
}
TEST_F(ModAtla, FullChipInventoryDoesNotConsumeSphereOrRegistry) {
    int chip = -1;
    for (int i = 0; i < 128; ++i) if (GetEditAtraChipData(0, i)) { chip = i; break; }
    ASSERT_GE(chip, 0);
    save->GetDngStatus()->atra_registry[0][0].id = 40 + chip;
    std::fill_n(save->GetElemData(0), 128, chip);
    const auto before = std::make_unique<CSaveData>(*save);
    EXPECT_FALSE(mods::CollectAtla(*save, *map, 0, 0, 0, error));
    EXPECT_EQ(std::memcmp(before.get(), save.get(), sizeof(CSaveData)), 0);
    EXPECT_EQ(map->atra[0].used, 1);
    EXPECT_EQ(map->events[0].kind, DNG_EVENT_ATRA);
    save->GetElemData(0)[127] = -1;
    ASSERT_TRUE(mods::CollectAtla(*save, *map, 0, 0, 0, error)) << error;
    EXPECT_EQ(save->GetElemData(0)[127], chip);
}
}
