#include <gtest/gtest.h>
#include <cstring>
#include "mods/lighting_control.hpp"
namespace {
mods::LightingState Initial() {
    mods::LightingState state;
    state.view_scale = 4;
    for (auto &side : state.sides) side.fog = {100, 500, 0, 255};
    return state;
}
TEST(ModLightingControl, FrontBackConflictAndLateInvalidFieldRollBackTogether) {
    auto state = Initial();
    mods::LightingControl control;
    std::string error;
    const std::vector<mods::GameCommand> first{{"dungeon.lighting_set",
        {{"side", "back"}, {"ambient", {10, 20, 30}}}, "first"}};
    ASSERT_TRUE(control.Apply(state, first, error)) << error;
    const auto before = mods::ReadLightingSnapshot(state);
    const auto owners = control.Owners();
    const std::vector<mods::GameCommand> conflicting{{"dungeon.lighting_set",
        {{"side", "both"}, {"ambient", {40, 50, 60}}}, "second"}};
    EXPECT_FALSE(control.Apply(state, conflicting, error));
    EXPECT_NE(error.find("first"), std::string::npos);
    EXPECT_EQ(mods::ReadLightingSnapshot(state), before);
    EXPECT_EQ(control.Owners(), owners);
    const std::vector<mods::GameCommand> rounded{{"dungeon.lighting_set",
        {{"fog", {{"near", 100.000001}, {"far", 100.000002}}}}, "first"}};
    EXPECT_FALSE(control.Apply(state, rounded, error));
    EXPECT_EQ(mods::ReadLightingSnapshot(state), before);
    const std::vector<mods::GameCommand> invalid{
        {"dungeon.lighting_set", {{"view_scale", 2}}, "first"},
        {"dungeon.lighting_set", {{"fog", {{"near", 600}}}}, "first"}};
    EXPECT_FALSE(control.Apply(state, invalid, error));
    EXPECT_EQ(mods::ReadLightingSnapshot(state), before);
    EXPECT_EQ(control.Owners(), owners);
}
TEST(ModLightingControl, NormalizesDirectionsAndRestoresStoredFloatValues) {
    auto state = Initial();
    mods::LightingControl control;
    std::string error;
    const std::vector<mods::GameCommand> set{{"dungeon.lighting_set",
        {{"side", "front"}, {"ambient", {0.1, 0.2, 0.3}}, {"view_scale", 1.1},
         {"lights", {{{"light", 2}, {"direction", {3, 4, 0}}, {"color", {11, 22, 33}}}}}}, "first"}};
    ASSERT_TRUE(control.Apply(state, set, error)) << error;
    EXPECT_FLOAT_EQ(state.sides[0].direction[2][0], 0.6f);
    EXPECT_FLOAT_EQ(state.sides[0].direction[2][1], 0.8f);
    EXPECT_FLOAT_EQ(state.sides[0].color[2][1], 22);
    EXPECT_FLOAT_EQ(state.sides[1].color[2][1], 0);
    const std::vector<mods::GameCommand> reset{{"dungeon.lighting_reset", nlohmann::json::object(), "first"}};
    ASSERT_TRUE(control.Apply(state, reset, error)) << error;
    EXPECT_EQ(mods::ReadLightingSnapshot(state), mods::ReadLightingSnapshot(Initial()));
    EXPECT_FALSE(control.Active());
}
TEST(ModLightingControl, NativeChangesSurviveUnrelatedEditsAndOwnerRetirement) {
    auto state = Initial();
    mods::LightingControl control;
    std::string error;
    const std::vector<mods::GameCommand> set{{"dungeon.lighting_set",
        {{"side", "front"}, {"ambient", {10, 20, 30}}}, "first"}};
    ASSERT_TRUE(control.Apply(state, set, error));
    state.sides[0].ambient = {40, 50, 60};
    const std::vector<mods::GameCommand> other{{"dungeon.lighting_set",
        {{"side", "back"}, {"background", {1, 2, 3}}}, "second"}};
    ASSERT_TRUE(control.Apply(state, other, error));
    control.Retain(state, {});
    EXPECT_EQ(state.sides[0].ambient, (std::array<float, 3>{40, 50, 60}));
    EXPECT_EQ(state.sides[1].background, (std::array<unsigned char, 3>{0, 0, 0}));
    EXPECT_FALSE(control.Active());
}
TEST(ModLightingControl, InvalidVectorsAndDormantPathsPreserveStateAndRng) {
    auto state = Initial();
    std::array<unsigned char, sizeof(state)> bytes{};
    std::memcpy(bytes.data(), &state, sizeof(state));
    mods::LightingControl control;
    std::string error;
    srand(81); const int expected = rand(); srand(81);
    EXPECT_TRUE(control.Apply(state, {}, error));
    control.Retain(state, {});
    EXPECT_EQ(std::memcmp(bytes.data(), &state, sizeof(state)), 0);
    EXPECT_EQ(rand(), expected);
    EXPECT_FALSE(mods::LightingControl::Validate("dungeon.lighting_set",
        {{"lights", {{{"light", 0}, {"direction", {0, 0, 0}}}}}}).empty());
    EXPECT_FALSE(mods::LightingControl::Validate("dungeon.lighting_set",
        {{"background", {1.1, 2, 3}}}).empty());
    EXPECT_FALSE(mods::LightingControl::Validate("dungeon.lighting_set", {{"side", "front"}}).empty());
    EXPECT_FALSE(mods::LightingControl::Validate("dungeon.lighting_set", {{"view_scale", 11}}).empty());
}
}
