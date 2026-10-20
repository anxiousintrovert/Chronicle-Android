#include <gtest/gtest.h>
#include <array>
#include <vector>
#include <stdexcept>
#include "../expanded_effect_voices.hpp"
#include "chronicle/monster_audio.hpp"
#include "audio/mixer.hpp"
#include "audio_retail.hpp"
#include "audio_retail_fixture.hpp"
#include "sound.hpp"

TEST(ModMonsterAudio, LogicalLeasesSeparateHighIdsAndEvictBeforeReuse) {
    chronicle_audio::ExpandedEffectVoices routing;
    std::vector<int> stopped;
    auto stop = [&](chronicle_audio::EffectIdentity key, unsigned) { stopped.push_back(key.voice); };
    for (int i = 0; i < 256; ++i) {
        EXPECT_EQ(routing.Play({10, 0, 0, i * 2}, stop), i);
    }
    EXPECT_TRUE(stopped.empty());
    EXPECT_EQ(routing.Find({10, 0, 0, 0}), 0);
    EXPECT_EQ(routing.Find({10, 0, 0, 256}), 128);
    EXPECT_EQ(routing.Play({10, 0, 0, 0}, stop), 0); // refresh oldest lease
    EXPECT_EQ(routing.Play({10, 0, 1, 512}, stop), 1);
    ASSERT_EQ(stopped.size(), 1);
    EXPECT_EQ(stopped[0], 2);
    EXPECT_EQ(routing.Find({10, 0, 0, 2}), -1);
    routing.Forget({10, 0, 0, 256});
    EXPECT_EQ(routing.Find({10, 0, 0, 256}), -1);
    EXPECT_EQ(routing.Find({10, 0, 0, 0}), 0);
    routing.Clear(stop);
    EXPECT_EQ(routing.Find({10, 0, 0, 0}), -1);
}

TEST(ModMonsterAudio, OfficialMixerKeepsSlotZeroAndSlot128Independent) {
    using namespace chronicle::monsters;
    auto &mixer = audio::DefaultMixer();
    CSound sound;
    PrepareEffectVoicesForMonsterSlots(16);
    EXPECT_FALSE(ExpandedEffectVoicesActive());
    EXPECT_THROW(PrepareEffectVoicesForMonsterSlots(257), std::invalid_argument);
    PrepareEffectVoicesForMonsterSlots(256);
    EXPECT_TRUE(ExpandedEffectVoicesActive());
    sound.Step();
    mixer.Reset();
    auto bank = chronicle_audio::CreateBank(chronicle_audio::fixture::MakeHd({}), chronicle_audio::fixture::SquareVag(2));
    ASSERT_NE(bank, nullptr);
    mixer.BindBank(10, bank);
    std::array<float, 8192> output{};
    sound.SE_Play(10, 0, 0, 64, 127, 127, 0);
    sound.SE_Play(10, 0, 0, 64, 127, 127, 256);
    sound.Step();
    mixer.Render(output.data(), 4096);
    EXPECT_EQ(mixer.ActiveVoices(), 2);
    sound.SE_Stop(10, 0, 0, 256);
    sound.Step();
    for (int i = 0; i < 10; ++i) mixer.Render(output.data(), 4096);
    EXPECT_EQ(mixer.ActiveVoices(), 1);
    // Repeated stale control messages must not reach the surviving low slot.
    sound.SE_SetVol(10, 0, 0, 0, 256);
    sound.SE_Stop(10, 0, 0, 256);
    sound.Step();
    mixer.Render(output.data(), 4096);
    EXPECT_EQ(mixer.ActiveVoices(), 1);
    PrepareEffectVoicesForMonsterSlots(16);
    sound.Step();
    for (int i = 0; i < 10; ++i) mixer.Render(output.data(), 4096);
    EXPECT_EQ(mixer.ActiveVoices(), 0);
    EXPECT_FALSE(ExpandedEffectVoicesActive());
    mixer.Reset();
}
