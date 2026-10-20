#include <gtest/gtest.h>
#include "../mods/retail_sound.hpp"
#include <cstring>
#include "audio_retail.hpp"
#include "data_fixture.hpp"
#include "audio/mixer.hpp"
#include "sound.hpp"
#include <array>
#include <algorithm>
#include "audio_retail_fixture.hpp"

TEST(ModRetailSound, RejectsTableTailsUnusedRowsAndInvalidSelectors) {
    std::string error;
    EXPECT_FALSE(mods::ResolveRetailEffect(-1, -1, -1, -1, error));
    EXPECT_FALSE(mods::ResolveRetailEffect(2800, -1, -1, -1, error));
    EXPECT_FALSE(mods::ResolveRetailEffect(299, 0, -1, -1, error));
    EXPECT_FALSE(mods::ResolveRetailEffect(299, 1, -1, -1, error));
    EXPECT_FALSE(mods::ResolveRetailEffect(344, -1, 3, -1, error));
    EXPECT_FALSE(mods::ResolveRetailEffect(343, -1, 3, -1, error)); // terminator
    EXPECT_FALSE(mods::ResolveRetailEffect(407, -1, -1, 6, error));
    EXPECT_FALSE(mods::ResolveRetailEffect(406, -1, -1, 6, error)); // terminator
    EXPECT_FALSE(mods::ResolveRetailEffect(100, 2, -1, -1, error));
    EXPECT_FALSE(mods::ResolveRetailEffect(300, -1, 101, -1, error));
    EXPECT_FALSE(mods::ResolveRetailEffect(400, -1, -1, 11, error));
    EXPECT_FALSE(mods::ResolveRetailEffect(404, -1, -1, 0, error)); // unused voice entry
}
TEST(ModRetailSound, ReturnsDetachedNativeRowsWithoutChangingTablesOrRandomStream) {
    std::string error;
    const auto before = voice_info[0][0];
    srand(981); const auto expected = rand(); srand(981);
    auto info = mods::ResolveRetailEffect(400, -1, -1, 0, error);
    ASSERT_TRUE(info) << error;
    EXPECT_EQ(info->bank, 23);
    EXPECT_EQ(info->prog, 20);
    info->bank = 1;
    EXPECT_EQ(std::memcmp(&before, &voice_info[0][0], sizeof(before)), 0);
    EXPECT_EQ(rand(), expected);
    // Absent chapter tables follow native lookup's fixed-table fallback.
    const auto fallback = mods::ResolveRetailEffect(300, -1, 5, -1, error);
    if (se_info[300].bank >= 0 && se_info[300].prog >= 0) {
        ASSERT_TRUE(fallback);
        EXPECT_EQ(fallback->bank, se_info[300].bank);
        EXPECT_EQ(fallback->prog, se_info[300].prog);
    } else EXPECT_FALSE(fallback);
}

TEST(ModRetailSound, BankReadinessUsesKeyThenSplitFallbackAndRejectsUnloadedSamples) {
    auto bank = chronicle_audio::CreateBank(chronicle_audio::fixture::MakeHd({.programs=2, .key_low=30, .key_high=90}), chronicle_audio::fixture::SquareVag(2));
    ASSERT_NE(bank, nullptr);
    EXPECT_TRUE(mods::RetailEffectHasSamples(bank.get(), 1, 64));
    EXPECT_TRUE(mods::RetailEffectHasSamples(bank.get(), 1, 0)); // native split-index fallback
    EXPECT_FALSE(mods::RetailEffectHasSamples(bank.get(), 1, 29));
    EXPECT_FALSE(mods::RetailEffectHasSamples(bank.get(), 2, 64));
    EXPECT_FALSE(mods::RetailEffectHasSamples(nullptr, 0, 0));
    EXPECT_FALSE(mods::RetailEffectHasSamples(bank.get(), -1, 0));
    EXPECT_FALSE(mods::RetailEffectHasSamples(bank.get(), 0, 128));
    std::string error;
    const SND_SE_INFO row{0, 0, 0, 10, -1};
    EXPECT_FALSE(mods::RetailEffectReady(row, 127, error));
    EXPECT_NE(error.find("not loaded"), std::string::npos);
    EXPECT_FALSE(mods::RetailEffectReady(row, 128, error));
    EXPECT_NE(error.find("invalid"), std::string::npos);
}

TEST(ModRetailSound, ExplicitDispatchWaitsForNativeStepAndRejectsMissingProgram) {
    const auto hd = chronicle_audio::fixture::MakeHd({});
    const auto bd = chronicle_audio::fixture::SquareVag(2);
    const std::string list = "test_g.hd\n";
    auto pack = datafix::MakePack({{"sound.txt", {list.begin(), list.end()}}, {"test_g.hd", hd}, {"test_g.bd", bd}});
    CSound sound;
    auto &mixer = audio::DefaultMixer();
    sound.Step(); mixer.Reset();
    char name[] = "sound.txt";
    sound.LoadSoundFileFromPack(name, reinterpret_cast<unsigned int *>(pack.data()));
    mixer.SetVolume(10, audio::kFullPortVolume);
    const SND_SE_INFO row{0, 0, 0, 10, -1};
    std::string error;
    ASSERT_TRUE(mods::RetailEffectReady(row, 127, error)) << error;
    ASSERT_TRUE(mods::QueueRetailEffect(row, 127, error)) << error;
    std::array<float, 8192> output{};
    mixer.Render(output.data(), 4096);
    EXPECT_EQ(mixer.ActiveVoices(), 0);
    EXPECT_TRUE(std::ranges::all_of(output, [](float sample) { return sample == 0; }));
    sound.Step();
    mixer.Render(output.data(), 4096);
    EXPECT_GT(mixer.ActiveVoices(), 0);
    EXPECT_TRUE(std::ranges::any_of(output, [](float sample) { return sample != 0; }));
    auto missing = row; missing.bank = 100;
    EXPECT_FALSE(mods::QueueRetailEffect(missing, 127, error));
    sound.SE_Stop(10, 0, 0, 0); sound.Step();
    for (int i = 0; i < 10; ++i) mixer.Render(output.data(), 4096);
    EXPECT_EQ(mixer.ActiveVoices(), 0);
    mixer.Reset();
}
