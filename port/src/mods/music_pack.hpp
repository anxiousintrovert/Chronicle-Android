#pragma once
#include <cstdint>
#include <memory>
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
struct PreparedMusicPack { std::vector<MusicPackEntry> entries; std::string config; };
// Detached preflight. No native buffers, mixer ports or playback state mutate.
std::shared_ptr<PreparedMusicPack> PrepareMusicPack(std::span<const std::uint8_t> bytes, int set, std::string &error);
}
