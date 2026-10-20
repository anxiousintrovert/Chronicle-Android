#pragma once
#include "audio/pcm.hpp"
namespace mods {
// Input <=32 MiB; stereo output <=max_bytes (maximum 32 MiB). Decoder scratch
// has a separate 8 MiB budget. Never publishes partial output on rejection.
std::shared_ptr<audio::PcmTrack> DecodeAudio(std::span<const std::uint8_t> input,
    std::string_view extension, std::size_t max_bytes, std::string &error);
}
