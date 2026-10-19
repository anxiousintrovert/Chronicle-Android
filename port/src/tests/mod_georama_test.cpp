#include <gtest/gtest.h>

#include <cstring>

#include "mods/api.hpp"
#include "mods/georama.hpp"
#include "savedata.hpp"

namespace {
using Json = nlohmann::json;

class ModGeorama : public testing::Test {
protected:
    std::unique_ptr<CSaveData> save = std::make_unique<CSaveData>();
    mods::GeoramaState         scene;
    std::string                error;

    void SetUp() override {
        scene.town = 0;
        scene.plots[0].available = true;
        scene.plots[0].elements[0] = {0, -1, false};
        scene.plots[0].elements[1] = {1, 0, false};
        save->GetEditPartsInfo(0, 0)->element_enabled[0] = 0;
        save->GetEditPartsInfo(0, 0)->element_enabled[1] = 0;
    }

    mods::GameCommand Set(int element, bool enabled) {
        return {
            "georama.element_set", {{"town", 0}, {"plot", 0}, {"element", element}, {"enabled", enabled}}
        };
    }

    bool Apply(std::vector<mods::GameCommand> commands) {
        return mods::ApplySave(*save, commands, 0, error, nullptr, &scene);
    }
};

TEST_F(ModGeorama, OrderedPrerequisitesPublishSaveAndSceneTogether) {
    EXPECT_FALSE(Apply({Set(1, true)}));
    ASSERT_TRUE(Apply({Set(0, true), Set(1, true)})) << error;
    EXPECT_TRUE(scene.plots[0].elements[1].enabled);
    EXPECT_EQ(save->GetEditPartsInfo(0, 0)->element_enabled[1], 1);
    EXPECT_FALSE(Apply({Set(0, false)}));
    ASSERT_TRUE(Apply({Set(1, false), Set(0, false)})) << error;
    EXPECT_FALSE(scene.plots[0].elements[0].enabled);
    EXPECT_EQ(save->GetEditPartsInfo(0, 0)->element_enabled[0], 0);
}

TEST_F(ModGeorama, LaterFailureRollsBackEntireSaveAndScene) {
    const auto before = std::make_unique<CSaveData>(*save);
    EXPECT_FALSE(Apply({Set(0, true), Set(1, true), Set(5, true)}));
    EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    EXPECT_FALSE(scene.plots[0].elements[0].enabled);
    EXPECT_FALSE(scene.plots[0].elements[1].enabled);
}

TEST_F(ModGeorama, QueriesAreDetachedAndPreserveSaveAndRng) {
    const auto before = std::make_unique<CSaveData>(*save);
    srand(741);
    const int expected = rand();
    srand(741);
    auto result = mods::ReadSave(*save, {
    },
                                 "georama.elements_get", {{"town", 0}, {"plot", 0}}, &scene);
    ASSERT_EQ(result["elements"].size(), 6);
    EXPECT_EQ(result["elements"][1]["required_element"], 0);
    EXPECT_EQ(result["elements"][5]["optional"], false);
    result["elements"][0]["enabled"] = true;
    EXPECT_FALSE(scene.plots[0].elements[0].enabled);
    EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    EXPECT_EQ(rand(), expected);
}

TEST_F(ModGeorama, RejectsAbsentScenePlotsAndMalformedPrerequisites) {
    EXPECT_FALSE(mods::ApplySave(*save, {Set(0, true)}, 0, error));
    scene.town = 1;
    EXPECT_FALSE(Apply({Set(0, true)}));
    scene.town = 0;
    scene.plots[0].available = false;
    EXPECT_FALSE(Apply({Set(0, true)}));
    scene.plots[0].available = true;
    scene.plots[0].elements[1].required = 6;
    EXPECT_FALSE(Apply({Set(1, true)}));
    scene.plots[0].elements[0].id = 36;
    EXPECT_FALSE(Apply({Set(0, true)}));
}

TEST(ModGeoramaValidation, RejectsOutOfBoundsTypesAndUnknownFields) {
    const Json valid = {
        {"town",    0   },
        {"plot",    0   },
        {"element", 0   },
        {"enabled", true}
    };
    EXPECT_TRUE(mods::ValidateOperation("georama.element_set", valid).empty());
    for (const auto &key : {"town", "plot", "element"}) {
        for (const Json &value : {Json(-1), Json(24), Json(0.5), Json("0"), Json(true)}) {
            auto bad = valid;
            bad[key] = value;
            EXPECT_FALSE(mods::ValidateOperation("georama.element_set", bad).empty());
        }
    }
    auto bad = valid;
    bad["enabled"] = 1;
    EXPECT_FALSE(mods::ValidateOperation("georama.element_set", bad).empty());
    bad = valid;
    bad["extra"] = 0;
    EXPECT_FALSE(mods::ValidateOperation("georama.element_set", bad).empty());
}
} // namespace
