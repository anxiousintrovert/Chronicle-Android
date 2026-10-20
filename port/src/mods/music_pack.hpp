#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "audio/hdbank.hpp"
#include "audio/sq.hpp"
namespace mods {
struct MusicPackEntry {
    char slot;
    std::string name;
    std::shared_ptr<audio::Bank> bank;
    std::shared_ptr<audio::SqFile> sequence;
};
struct MusicReverb { int mode = 0; int depth = 0; };
struct PreparedMusicPack {
    std::vector<MusicPackEntry> entries;
    // Absent config preserves current reverb; an empty config resets it.
    std::optional<MusicReverb> reverb;
};
bool ParseMusicConfig(std::string_view text, MusicReverb &result, std::string &error);
// Detached preflight. No native buffers, mixer ports or playback state mutate.
std::shared_ptr<PreparedMusicPack> PrepareMusicPack(std::span<const std::uint8_t> bytes, int set, std::string &error);
}
