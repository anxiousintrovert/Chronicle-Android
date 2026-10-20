#include <gtest/gtest.h>
#include <cstring>
#include "mods/georama_placement.hpp"
#include "editarea.hpp"
#include "editground.hpp"
#include "editloop.hpp"
#include "editmapscript.hpp"
#include "editpartsinfo.hpp"
#include "gamemode.hpp"
#include "main.hpp"
#include "savedata.hpp"

extern s32 mode;
namespace {
class ModGeoramaPlacement : public testing::Test {
protected:
    std::unique_ptr<CSaveData> save = std::make_unique<CSaveData>();
    std::unique_ptr<CEditGround> ground = std::make_unique<CEditGround>();
    std::array<CMapParts, 24> templates{};
    CEditPartsInfo previous_info = EditPartsInfo;
    CSaveData *previous_save = SaveData;
    CEditGround *previous_ground = pEditGround;
    int previous_mode = mode, previous_game_mode = GameMode;
    int previous_map = MapNo, previous_town = NowEditMap;
    std::string error;
    mods::GameCommand command{"georama.place",
        {{"town", 0}, {"part", 0}, {"rotation", 0}, {"position", {0, 0, 0}}}, "test.placement"};
    void SetUp() override {
        SaveData = save.get(); pEditGround = ground.get();
        mode = GAME_MODE_EDIT; GameMode = ED_MODE_WALK; MapNo = NowEditMap = 0;
        EditPartsInfo = {};
        for (int i = 0; i < 24; ++i) {
            EditPartsInfo.parts[i].parts_no = i;
            templates[i].handle = -1;
        }
        ground->map_no = 0; ground->parts_info = &EditPartsInfo;
        ground->plot_parts = templates.data(); ground->effect_count = 0;
        for (auto &part : ground->parts) part.handle = -1;
        for (auto &area : ground->areas) area = nullptr;
        templates[0].handle = 0; templates[0].parts_no = 0;
        templates[0].info = &EditPartsInfo.parts[0];
        EditPartsInfo.parts[0].stock = 100;
    }
    void TearDown() override {
        SaveData = previous_save; pEditGround = previous_ground; EditPartsInfo = previous_info;
        mode = previous_mode; GameMode = previous_game_mode; MapNo = previous_map; NowEditMap = previous_town;
    }
    void RejectWithoutMutation(const std::string &reason) {
        const auto before = std::make_unique<CSaveData>(*save);
        const auto parts_before = EditPartsInfo;
        const auto geometry = std::make_unique<CEditGround>(*ground);
        std::array<unsigned char, sizeof(templates)> templates_before{};
        std::memcpy(templates_before.data(), templates.data(), sizeof(templates));
        srand(907); const int expected = rand(); srand(907);
        EXPECT_FALSE(mods::ApplyGeoramaPlacement({command}, 0, error));
        EXPECT_NE(error.find(reason), std::string::npos) << error;
        EXPECT_EQ(std::memcmp(before.get(), save.get(), sizeof(CSaveData)), 0);
        EXPECT_EQ(std::memcmp(&parts_before, &EditPartsInfo, sizeof(EditPartsInfo)), 0);
        EXPECT_EQ(std::memcmp(geometry.get(), ground.get(), sizeof(CEditGround)), 0);
        EXPECT_EQ(std::memcmp(templates_before.data(), templates.data(), sizeof(templates)), 0);
        EXPECT_EQ(rand(), expected);
    }
};
TEST_F(ModGeoramaPlacement, CapacityFailuresPreserveStockSaveGeometryAndRng) {
    for (auto &part : ground->parts) { part.handle = 0; part.parts_no = 0; }
    RejectWithoutMutation("table is full");
    for (int i = 64; i < 128; ++i) ground->parts[i].handle = -1;
    RejectWithoutMutation("request evaluation capacity");
}
TEST_F(ModGeoramaPlacement, SceneAndNativeFitFailuresPreserveState) {
    ground->effect_count = 1;
    RejectWithoutMutation("build effect");
    ground->effect_count = 0; MapNo = 1;
    RejectWithoutMutation("stable walking scene");
    MapNo = 0;
    RejectWithoutMutation("native placement fit");
    // An in-area rejected fit rotates the template before returning failure.
    CEditArea area{};
    area.SetSize(1, 1, 100, 100);
    ground->areas[0] = &area;
    EditPartsInfo.parts[0].width = EditPartsInfo.parts[0].height = 4;
    command.arguments["rotation"] = 2;
    RejectWithoutMutation("native placement fit");
    ground->areas[0] = nullptr;
    command = {"georama.remove", {{"town", 0}, {"slot", 0}}, "test.placement"};
    RejectWithoutMutation("no longer occupied");
}
TEST_F(ModGeoramaPlacement, DetachedReadAndExclusiveWriteContract) {
    const auto result = mods::ReadGeoramaPlacement();
    EXPECT_EQ(result["parts"][0]["available"], 100);
    EXPECT_TRUE(result["placements"].empty());
    auto detached = result;
    detached["parts"][0]["stock"] = 0;
    EXPECT_EQ(EditPartsInfo.parts[0].stock, 100);
    EXPECT_FALSE(mods::ApplyGeoramaPlacement({command, command}, 0, error));
    EXPECT_FALSE(mods::ApplyGeoramaPlacement({command}, 1, error));
    command.owner.clear();
    RejectWithoutMutation("only engine write");
    EXPECT_FALSE(mods::ValidateGeoramaPlacement("georama.place",
        {{"town", 0}, {"part", 0}, {"rotation", 4}, {"position", {0, 0, 0}}}).empty());
    EXPECT_FALSE(mods::ValidateGeoramaPlacement("georama.remove", {{"town", 0}, {"slot", 128}}).empty());
}
}
