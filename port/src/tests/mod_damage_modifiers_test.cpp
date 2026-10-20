#include <gtest/gtest.h>
#include "mods/damage_modifiers.hpp"
#include <bit>
#include <cstdlib>
#include <limits>
using Json = nlohmann::json;
namespace {
Json Formula(const char *id, const char *operation, double value, int priority = 0) {
    return {{"id", id}, {"operation", operation}, {"value", value}, {"priority", priority}};
}
Json RandomFormula() {
    return {{"id", "roll"}, {"operation", "random_multiply"}, {"min_value", 0.5}, {"max_value", 1.5}, {"seed", 7}};
}
}
TEST(ModDamageModifiers, EmptyAndUnmatchedRulesPreserveExactFloatBitsAndNativeRng) {
    mods::DamageModifiers registry; std::string error;
    const std::array<std::uint32_t, 6> values{0, 0x80000000u, 0x7fc01234u, 0x7f800000u, 0x4b800000u, 0x3faaaaabu};
    for (auto bits : values) EXPECT_EQ(std::bit_cast<std::uint32_t>(registry.Apply(std::bit_cast<float>(bits), {})), bits);
    auto filtered = Formula("blocked", "set", 20); filtered["weapon_id"] = 7;
    ASSERT_TRUE(registry.Set("alpha", filtered, error));
    srand(91); const int next = rand(); srand(91);
    for (auto bits : values) EXPECT_EQ(std::bit_cast<std::uint32_t>(registry.Apply(std::bit_cast<float>(bits), {})), bits);
    EXPECT_EQ(rand(), next);
}
TEST(ModDamageModifiers, PriorityUpdatesReorderAndOwnerRemovalDoesNotRemovePeers) {
    mods::DamageModifiers registry; std::string error;
    ASSERT_TRUE(registry.Set("zeta", Formula("same", "add", 10, 0), error));
    ASSERT_TRUE(registry.Set("alpha", Formula("same", "multiply", 2, 1), error));
    EXPECT_EQ(registry.Apply(5, {}), 30);
    ASSERT_TRUE(registry.Set("zeta", Formula("same", "add", 10, 2), error));
    EXPECT_EQ(registry.Size(), 2); EXPECT_EQ(registry.Apply(5, {}), 20);
    EXPECT_FALSE(registry.Remove("someone", "same"));
    EXPECT_TRUE(registry.Remove("zeta", "same")); EXPECT_EQ(registry.Apply(5, {}), 10);
    registry.Retain({}); EXPECT_TRUE(registry.Empty());
    EXPECT_EQ(registry.Apply(5, {}), 5);
    mods::DamageModifiers reverse;
    ASSERT_TRUE(registry.Set("zeta", Formula("same", "add", 10), error));
    ASSERT_TRUE(registry.Set("alpha", Formula("same", "multiply", 2), error));
    ASSERT_TRUE(reverse.Set("alpha", Formula("same", "multiply", 2), error));
    ASSERT_TRUE(reverse.Set("zeta", Formula("same", "add", 10), error));
    EXPECT_EQ(registry.Apply(5, {}), 20); EXPECT_EQ(reverse.Apply(5, {}), 20);
}
TEST(ModDamageModifiers, FiltersUseOriginalHealthAndCannotResurrectBlockedDamage) {
    mods::DamageModifiers registry; std::string error;
    auto formula = Formula("execute", "set", 100);
    formula["weapon_id"] = 258; formula["native_model_id"] = 4; formula["character_id"] = 2;
    formula["target_hp_below_percent"] = 50;
    ASSERT_TRUE(registry.Set("alpha", formula, error));
    mods::DamageContext context{258, 4, 2, 49, 100, 0};
    EXPECT_EQ(registry.Apply(10, context), 100);
    context.hp = 50; EXPECT_EQ(registry.Apply(10, context), 10);
    context.hp = 49; context.model = 5; EXPECT_EQ(registry.Apply(10, context), 10);
    context.model = 4; context.weapon = 259; EXPECT_EQ(registry.Apply(10, context), 10);
    context.weapon = 258; context.character = 1; EXPECT_EQ(registry.Apply(10, context), 10);
    context.character = 2; context.max_hp = 0; EXPECT_EQ(registry.Apply(10, context), 10);
    context.max_hp = 100; EXPECT_EQ(registry.Apply(0, context), 0);
    ASSERT_TRUE(registry.Set("alpha", Formula("zero", "add", -1000, -1), error));
    EXPECT_EQ(registry.Apply(10, context), 0);
    registry.Clear(); ASSERT_TRUE(registry.Set("alpha", Formula("cap", "multiply", 100), error));
    EXPECT_EQ(registry.Apply(std::numeric_limits<float>::max(), {}), 1000000);
}
TEST(ModDamageModifiers, SeededGoldenSequenceAndIdempotentRegistrationPreserveNativeRng) {
    mods::DamageModifiers registry, control; std::string error;
    ASSERT_TRUE(registry.Set("alpha", RandomFormula(), error));
    ASSERT_TRUE(control.Set("alpha", RandomFormula(), error));
    constexpr std::array<std::uint32_t, 6> expected{0x4300ed0eu,0x42d72eb8u,0x42f28b08u,0x42e10074u,0x43042215u,0x4302aa98u};
    mods::DamageModifiers filtered;
    auto filter = RandomFormula(); filter["weapon_id"] = 7;
    ASSERT_TRUE(filtered.Set("alpha", filter, error));
    EXPECT_EQ(filtered.Apply(100, {}), 100); EXPECT_EQ(filtered.State()[0]["draws"], 0);
    mods::DamageContext matching; matching.weapon = 7;
    EXPECT_EQ(std::bit_cast<std::uint32_t>(filtered.Apply(100, matching)), expected[0]);
    srand(17); const int next = rand(); srand(17);
    for (auto bits : expected) {
        ASSERT_TRUE(registry.Set("alpha", RandomFormula(), error));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(registry.Apply(100, {})), bits);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(control.Apply(100, {})), bits);
    }
    EXPECT_EQ(rand(), next); EXPECT_EQ(registry.State()[0]["draws"], 6);
    auto detached = registry.State(); detached[0]["min_value"] = 100;
    EXPECT_EQ(registry.State()[0]["min_value"], 0.5);
    auto staged = registry;
    ASSERT_TRUE(staged.Set("beta", Formula("bonus", "add", 10), error));
    EXPECT_FALSE(staged.Set("beta", {{"id", "bad"}}, error));
    EXPECT_EQ(registry.Size(), 1); EXPECT_EQ(registry.Apply(100, {}), control.Apply(100, {}));
}
TEST(ModDamageModifiers, ChanceAndRampHaveBoundedReproducibleState) {
    mods::DamageModifiers registry; std::string error;
    auto chance = Formula("chance", "chance_multiply", 3); chance["chance"] = 0;
    ASSERT_TRUE(registry.Set("alpha", chance, error)); EXPECT_EQ(registry.Apply(10, {}), 10);
    chance["chance"] = 100; ASSERT_TRUE(registry.Set("alpha", chance, error)); EXPECT_EQ(registry.Apply(10, {}), 30);
    registry.Clear(); auto ramp = Formula("ramp", "ramp_multiply", 3);
    ramp["rate_per_second"] = 1; ramp["reset_after_frames"] = 120;
    ASSERT_TRUE(registry.Set("alpha", ramp, error)); mods::DamageContext context;
    context.frame = 100; EXPECT_EQ(registry.Apply(10, context), 10);
    context.frame = 160; EXPECT_EQ(registry.Apply(10, context), 20);
    ASSERT_TRUE(registry.Set("alpha", ramp, error));
    context.frame = 220; EXPECT_EQ(registry.Apply(10, context), 30);
    context.frame = 340; EXPECT_EQ(registry.Apply(10, context), 30);
    context.frame = 461; EXPECT_EQ(registry.Apply(10, context), 10);
    context.frame = 1; EXPECT_EQ(registry.Apply(10, context), 10);
}
TEST(ModDamageModifiers, RejectsAmbiguousFieldsAndEnforcesOwnerAndGlobalCapacity) {
    mods::DamageModifiers registry; std::string error;
    for (Json invalid : std::vector<Json>{Json::array(), {{"id","bad"}},
        {{"id","bad/id"},{"operation","set"},{"value",1}},
        {{"id","bad"},{"operation","set"},{"value",true}},
        {{"id","bad"},{"operation","set"},{"value",std::numeric_limits<double>::infinity()}},
        {{"id","bad"},{"operation","multiply"},{"value",101}},
        {{"id","bad"},{"operation","multiply"},{"value",1},{"chance",50}},
        {{"id","bad"},{"operation","multiply"},{"value",1},{"seed",1}},
        {{"id","bad"},{"operation","multiply"},{"value",1},{"priority",1.5}},
        {{"id","bad"},{"operation","multiply"},{"value",1},{"target_hp_below_percent",101}}}) {
        EXPECT_FALSE(registry.Set("alpha", invalid, error)); EXPECT_TRUE(registry.Empty());
    }
    auto random = RandomFormula(); random["chance"] = 10; EXPECT_FALSE(registry.Set("alpha", random, error));
    random = RandomFormula(); random["min_value"] = 2; EXPECT_FALSE(registry.Set("alpha", random, error));
    random = RandomFormula(); random["seed"] = 4294967296ULL; EXPECT_FALSE(registry.Set("alpha", random, error));
    for (int owner = 0; owner < 4; ++owner) for (int i = 0; i < 32; ++i)
        ASSERT_TRUE(registry.Set("owner" + std::to_string(owner), Formula(std::to_string(i).c_str(), "add", 1), error));
    EXPECT_EQ(registry.Size(), 128);
    EXPECT_FALSE(registry.Set("owner0", Formula("extra", "add", 1), error));
    EXPECT_FALSE(registry.Set("owner4", Formula("extra", "add", 1), error));
    EXPECT_TRUE(registry.Set("owner0", Formula("0", "add", 2), error)); EXPECT_EQ(registry.Size(), 128);
    const std::array<std::string,1> keep{"owner0"}; registry.Retain(keep); EXPECT_EQ(registry.Size(), 32);
    EXPECT_FALSE(registry.Set("owner0", Formula("extra", "add", 1), error));
}
