#include <gtest/gtest.h>
#include "../mods/audio_decode.hpp"
#include "mod_audio_decode_fixture.hpp"
#include <cstring>

namespace {
std::vector<std::uint8_t> Wav(unsigned channels = 1, unsigned rate = 48000) {
    std::vector<std::uint8_t> data;
    auto text = [&](const char *s) { data.insert(data.end(), s, s + std::strlen(s)); };
    auto number = [&](unsigned n, int bytes) { for (int i = 0; i < bytes; ++i) data.push_back((n >> (8 * i)) & 255); };
    text("RIFF"); number(36 + channels * 8, 4); text("WAVEfmt "); number(16, 4);
    number(1, 2); number(channels, 2); number(rate, 4); number(rate * channels * 2, 4);
    number(channels * 2, 2); number(16, 2); text("data"); number(channels * 8, 4);
    for (int frame = 0; frame < 4; ++frame)
        for (unsigned c = 0; c < channels; ++c) number(1000 + c * 100, 2);
    return data;
}
}
TEST(ModAudioDecode, WavNormalizesMonoAndFrontPairWithinExactBudget) {
    std::string error;
    for (unsigned channels : {1u, 2u, 6u}) {
        auto track = mods::DecodeAudio(Wav(channels), ".WAV", 16, error);
        ASSERT_NE(track, nullptr) << error;
        EXPECT_EQ(track->Frames(), 4);
        EXPECT_EQ(track->rate, 48000);
        for (int frame = 0; frame < 4; ++frame) {
            EXPECT_EQ(track->samples[frame * 2], 1000);
            EXPECT_EQ(track->samples[frame * 2 + 1], channels == 1 ? 1000 : 1100);
        }
    }
}
TEST(ModAudioDecode, RejectsInvalidAndOversizedInputWithoutPartialTrack) {
    std::string error;
    EXPECT_EQ(mods::DecodeAudio({}, ".wav", 16, error), nullptr);
    EXPECT_EQ(mods::DecodeAudio(Wav(), ".wav", 12, error), nullptr);
    EXPECT_EQ(mods::DecodeAudio(Wav(), ".wav", 0, error), nullptr);
    EXPECT_EQ(mods::DecodeAudio(Wav(), ".wav", 32 * 1024 * 1024 + 1, error), nullptr);
    EXPECT_EQ(mods::DecodeAudio(Wav(1, 7999), ".wav", 16, error), nullptr);
    EXPECT_EQ(mods::DecodeAudio(Wav(9), ".wav", 16, error), nullptr);
    EXPECT_EQ(mods::DecodeAudio(Wav(), ".mp3", 16, error), nullptr);
    for (auto type : {".wav", ".flac", ".ogg"}) {
        const std::array<std::uint8_t, 8> invalid{1, 2, 3};
        EXPECT_EQ(mods::DecodeAudio(invalid, type, 16, error), nullptr);
        EXPECT_FALSE(error.empty());
    }
}
TEST(ModAudioDecode, CompressedFixturesDecodeAndRespectOutputBudget) {
    std::string error;
    for (const auto &[bytes, type] : std::array{
             std::pair{std::span<const std::uint8_t>(kAudioFlac), ".flac"},
             std::pair{std::span<const std::uint8_t>(kAudioOgg), ".ogg"}}) {
        auto track = mods::DecodeAudio(bytes, type, 4096, error);
        ASSERT_NE(track, nullptr) << type << ": " << error;
        EXPECT_EQ(track->Frames(), 1024);
        EXPECT_EQ(track->rate, 48000);
        EXPECT_GT(track->samples[200], 7000);
        EXPECT_LT(track->samples[200], 9000);
        EXPECT_EQ(mods::DecodeAudio(bytes, type, 4000, error), nullptr);
        EXPECT_FALSE(error.empty());
    }
}

TEST(ModAudioDecode, BoundsForgedWavLengthAndRejectsTruncatedFlac) {
    std::string error;
    auto wav = Wav();
    // dr_wav clamps the claimed data size to available input. Verify that a
    // forged length never expands output beyond the actual four frames.
    wav[40] = 0xfc; wav[41] = 0xff; wav[42] = 0xff; wav[43] = 0x7f;
    auto bounded = mods::DecodeAudio(wav, ".wav", 16, error);
    ASSERT_NE(bounded, nullptr) << error;
    EXPECT_EQ(bounded->Frames(), 4);
    const auto truncated = std::span<const std::uint8_t>(kAudioFlac).first(sizeof(kAudioFlac) - 8);
    EXPECT_EQ(mods::DecodeAudio(truncated, ".flac", 4096, error), nullptr);
    EXPECT_FALSE(error.empty());
}
