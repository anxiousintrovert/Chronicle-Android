#include <gtest/gtest.h>
#include <array>
#include <fstream>
#include <chrono>
#include "mod_audio_decode_fixture.hpp"
#include <atomic>
#include <thread>
#include <cmath>
#include <limits>
#include "../mods/audio_streams.hpp"
#include "audio/mixer.hpp"

namespace {
audio::PcmTrack Tone() {
    audio::PcmTrack track;
    track.rate = 48000;
    track.samples.assign(16, 8192);
    return track;
}
std::array<float, 64> Render(audio::Mixer &mixer) {
    std::array<float, 64> output{};
    mixer.Render(output.data(), 32);
    return output;
}
}
TEST(ModAudioStreams, CopiesPcmAndIsolatesOwnersAndRetirement) {
    audio::Mixer mixer(48000);
    mods::AudioStreams streams(mixer);
    auto track = Tone();
    std::string error;
    ASSERT_TRUE(streams.Register("a", "tone", track, error)) << error;
    ASSERT_TRUE(streams.Register("b", "tone", track, error)) << error;
    track.samples.assign(16, 0);
    const auto handle = streams.Play("a", "tone", true, 0.5, 0, error);
    ASSERT_FALSE(handle.empty()) << error;
    for (float sample : Render(mixer)) EXPECT_NEAR(sample, 0.125f, 0.00001f);
    EXPECT_FALSE(streams.Stop("b", handle, error));
    EXPECT_TRUE(streams.State("b")["voices"].empty());
    const std::array<std::string, 1> retained{"b"};
    streams.Retain(retained);
    EXPECT_TRUE(streams.State("a")["sounds"].empty());
    EXPECT_EQ(streams.State("b")["sounds"].size(), 1);
    for (float sample : Render(mixer)) EXPECT_EQ(sample, 0);
    EXPECT_FALSE(streams.Stop("a", handle, error));
}
TEST(ModAudioStreams, PanEndPruningAndDestructionDetachChannels) {
    audio::Mixer mixer(48000);
    std::string error;
    {
        mods::AudioStreams streams(mixer);
        ASSERT_TRUE(streams.Register("a", "tone", Tone(), error));
        ASSERT_FALSE(streams.Play("a", "tone", false, 1, -1, error).empty());
        const auto output = Render(mixer);
        EXPECT_NEAR(output[0], 0.25f * std::sqrt(2.f), 0.00001f);
        EXPECT_EQ(output[1], 0);
        EXPECT_EQ(output.back(), 0);
        streams.Prune();
        EXPECT_TRUE(streams.State("a")["voices"].empty());
        ASSERT_FALSE(streams.Play("a", "tone", true, 1, 1, error).empty());
        EXPECT_GT(Render(mixer)[1], 0.3f);
    }
    for (float sample : Render(mixer)) EXPECT_EQ(sample, 0);
}
TEST(ModAudioStreams, RejectsInvalidInputsAndEnforcesOwnerLimits) {
    audio::Mixer mixer(48000);
    mods::AudioStreams streams(mixer);
    std::string error;
    auto track = Tone();
    EXPECT_FALSE(streams.Register("a", "../tone", track, error));
    track.samples.pop_back();
    EXPECT_FALSE(streams.Register("a", "odd", track, error));
    track = Tone();
    track.rate = 7999;
    EXPECT_FALSE(streams.Register("a", "rate", track, error));
    for (int i = 0; i < 16; ++i)
        ASSERT_TRUE(streams.Register("a", std::to_string(i), Tone(), error));
    EXPECT_FALSE(streams.Register("a", "overflow", Tone(), error));
    EXPECT_FALSE(streams.Register("a", "0", Tone(), error));
    EXPECT_TRUE(streams.Play("b", "0", true, 1, 0, error).empty());
    EXPECT_TRUE(streams.Play("a", "0", true, std::numeric_limits<double>::quiet_NaN(), 0, error).empty());
    EXPECT_TRUE(streams.Play("a", "0", true, 1, 2, error).empty());
    std::string first;
    for (int i = 0; i < 16; ++i) {
        auto handle = streams.Play("a", "0", true, 0, 0, error);
        ASSERT_FALSE(handle.empty());
        if (i == 0) first = handle;
    }
    EXPECT_TRUE(streams.Play("a", "0", true, 0, 0, error).empty());
    ASSERT_TRUE(streams.Stop("a", first, error));
    ASSERT_FALSE(streams.Play("a", "0", true, 0, 0, error).empty());
    streams.Clear();
    ASSERT_TRUE(streams.Register("a", "0", Tone(), error));
    EXPECT_NE(streams.Play("a", "0", true, 0, 0, error), first);
    EXPECT_FALSE(streams.Stop("a", first, error));
}
TEST(ModAudioStreams, DormantOwnerDoesNotAlterExistingMixerStream) {
    audio::Mixer baseline(48000), actual(48000);
    auto track = std::make_shared<audio::PcmTrack>(Tone());
    for (auto *mixer : {&baseline, &actual}) {
        mixer->SetStream(0, track);
        mixer->SetVolume(0, audio::kFullPortVolume);
        mixer->Play(0);
    }
    {
        mods::AudioStreams dormant(actual);
        EXPECT_EQ(Render(actual), Render(baseline));
        dormant.Clear();
        EXPECT_EQ(Render(actual), Render(baseline));
    }
    EXPECT_EQ(Render(actual), Render(baseline));
}

TEST(ModAudioStreams, GlobalLimitsDoNotPreventOtherOwnersFromStopping) {
    audio::Mixer mixer(48000);
    mods::AudioStreams streams(mixer);
    std::string error, first;
    for (int owner = 0; owner < 4; ++owner) {
        const auto name = std::to_string(owner);
        for (int id = 0; id < 16; ++id) {
            ASSERT_TRUE(streams.Register(name, std::to_string(id), Tone(), error));
            auto handle = streams.Play(name, std::to_string(id), true, 0, 0, error);
            ASSERT_FALSE(handle.empty());
            if (owner == 0 && id == 0) first = handle;
        }
    }
    EXPECT_FALSE(streams.Register("other", "tone", Tone(), error));
    EXPECT_TRUE(streams.Play("3", "0", true, 0, 0, error).empty());
    EXPECT_TRUE(streams.Stop("0", first, error));
    EXPECT_FALSE(streams.Play("0", "0", true, 0, 0, error).empty());
    streams.Clear();
    EXPECT_TRUE(streams.Register("other", "tone", Tone(), error));
}
TEST(ModAudioStreams, ConcurrentRenderingSurvivesRepeatedChannelRetirement) {
    audio::Mixer mixer(48000);
    std::atomic<bool> done{false};
    std::atomic<unsigned> rendered{0};
    std::jthread renderer([&] {
        while (!done.load()) {
            Render(mixer);
            ++rendered;
        }
    });
    // Ensure the worker is rendering before exercising removal and destruction.
    while (rendered.load() == 0) std::this_thread::yield();
    std::string error;
    for (int cycle = 0; cycle < 100; ++cycle) {
        mods::AudioStreams streams(mixer);
        EXPECT_TRUE(streams.Register("a", "tone", Tone(), error));
        auto handle = streams.Play("a", "tone", true, 0.5, 0, error);
        EXPECT_FALSE(handle.empty());
        if (cycle % 3 == 0) EXPECT_TRUE(streams.Stop("a", handle, error));
        else if (cycle % 3 == 1) streams.Retain({});
        // The third path retires an actively rendered channel in the destructor.
    }
    done = true;
    renderer.join();
    for (float sample : Render(mixer)) EXPECT_EQ(sample, 0);
}

TEST(ModAudioStreams, PackageSnapshotIsAtomicAndDoesNotReadInactiveAssets) {
    const auto root = std::filesystem::temp_directory_path() / ("mod-audio-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); } } cleanup{root};
    auto write = [&](const char *name, auto bytes) {
        std::ofstream out(root / name, std::ios::binary);
        out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    };
    write("tone.flac", std::span(kAudioFlac));
    audio::Mixer mixer(48000);
    mods::AudioStreams streams(mixer);
    mods::Package package;
    package.root = root; package.id = "test.audio";
    package.audio = {{"a", root / "tone.flac"}, {"b", root / "missing.ogg"}};
    std::string error;
    EXPECT_TRUE(streams.LoadPackage(package, error));
    EXPECT_TRUE(streams.State(package.id)["sounds"].empty());
    package.enabled = true;
    EXPECT_FALSE(streams.LoadPackage(package, error));
    EXPECT_TRUE(streams.State(package.id)["sounds"].empty()); // first decoded asset rolled back
    write("missing.ogg", std::span(kAudioOgg));
    ASSERT_TRUE(streams.LoadPackage(package, error)) << error;
    EXPECT_EQ(streams.State(package.id)["sounds"].size(), 2);
    EXPECT_FALSE(streams.LoadPackage(package, error));
    std::filesystem::remove(root / "tone.flac");
    ASSERT_FALSE(streams.Play(package.id, "a", true, 1, 0, error).empty());
    EXPECT_NEAR(Render(mixer)[0], 0.25f, 0.001f); // independent decoded snapshot
}
