#include "audio_decode.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include "dr_wav.h"
#include "dr_flac.h"
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"

namespace mods {
namespace {
constexpr std::size_t Limit = 32 * 1024 * 1024, Scratch = 8 * 1024 * 1024;
struct Budget { std::size_t used = 0; };
struct alignas(std::max_align_t) Header { std::size_t bytes; };
void *Allocate(std::size_t size, void *context) {
    auto &budget = *static_cast<Budget *>(context);
    if (size > Scratch - budget.used || sizeof(Header) > Scratch - budget.used - size) return nullptr;
    auto *header = static_cast<Header *>(std::malloc(sizeof(Header) + size));
    if (!header) return nullptr;
    header->bytes = size + sizeof(Header); budget.used += header->bytes;
    return header + 1;
}
void Free(void *pointer, void *context) {
    if (!pointer) return;
    auto *header = static_cast<Header *>(pointer) - 1;
    static_cast<Budget *>(context)->used -= header->bytes;
    std::free(header);
}
void *Reallocate(void *pointer, std::size_t size, void *context) {
    if (!pointer) return Allocate(size, context);
    if (!size) { Free(pointer, context); return nullptr; }
    // Count both old and new allocations during copying.
    auto *replacement = Allocate(size, context);
    if (!replacement) return nullptr;
    std::memcpy(replacement, pointer, std::min(size, (static_cast<Header *>(pointer) - 1)->bytes - sizeof(Header)));
    Free(pointer, context); return replacement;
}
template<class Reader>
std::shared_ptr<audio::PcmTrack> Read(unsigned rate, unsigned channels, std::size_t max_bytes,
                                    Reader read, std::string &error) {
    if (rate < 8000 || rate > 192000 || channels < 1 || channels > 8) {
        error = "audio requires 8..192 kHz and 1..8 source channels"; return {};
    }
    auto result = std::make_shared<audio::PcmTrack>();
    result->rate = static_cast<int>(rate); result->source_channels = static_cast<int>(channels);
    // Fixed output capacity avoids geometric growth beyond the caller's budget.
    result->samples.reserve(max_bytes / sizeof(std::int16_t));
    std::array<std::int16_t, 1024 * 8> block{};
    for (;;) {
        const auto frames = read(block.data(), 1024);
        if (frames == 0) break;
        if (frames > 1024 || frames > (max_bytes / sizeof(std::int16_t) - result->samples.size()) / 2) {
            error = "decoded audio exceeds the available PCM budget"; return {};
        }
        for (std::size_t i = 0; i < frames; ++i) {
            result->samples.push_back(block[i * channels]);
            result->samples.push_back(block[i * channels + (channels > 1 ? 1 : 0)]);
        }
    }
    if (result->samples.empty()) { error = "audio contains no decoded frames"; return {}; }
    error.clear(); return result;
}
}
std::shared_ptr<audio::PcmTrack> DecodeAudio(std::span<const std::uint8_t> input,
    std::string_view extension, std::size_t max_bytes, std::string &error) {
    if (input.empty() || input.size() > Limit || max_bytes < 4 || max_bytes > Limit) {
        error = "audio input or decoded budget is outside the 32 MiB limit"; return {};
    }
    std::string type(extension);
    std::ranges::transform(type, type.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    error = "cannot open audio within decoder scratch budget";
    Budget budget;
    if (type == ".wav") {
        drwav decoder{};
        const drwav_allocation_callbacks callbacks{&budget, Allocate, Reallocate, Free};
        if (!drwav_init_memory(&decoder, input.data(), input.size(), &callbacks)) return {};
        struct Close { drwav *p; ~Close() { drwav_uninit(p); } } close{&decoder};
        if (decoder.totalPCMFrameCount > max_bytes / 4) { error = "WAV frame count exceeds PCM budget"; return {}; }
        auto result = Read(decoder.sampleRate, decoder.channels, max_bytes,
            [&](auto *out, auto frames) { return drwav_read_pcm_frames_s16(&decoder, frames, out); }, error);
        if (result && result->Frames() != decoder.totalPCMFrameCount) { error = "truncated WAV frames"; return {}; }
        return result;
    }
    if (type == ".flac") {
        const drflac_allocation_callbacks callbacks{&budget, Allocate, Reallocate, Free};
        auto *decoder = drflac_open_memory(input.data(), input.size(), &callbacks);
        if (!decoder) return {};
        std::unique_ptr<drflac, decltype(&drflac_close)> close(decoder, drflac_close);
        if (decoder->totalPCMFrameCount > max_bytes / 4) { error = "FLAC frame count exceeds PCM budget"; return {}; }
        auto result = Read(decoder->sampleRate, decoder->channels, max_bytes,
            [&](auto *out, auto frames) { return drflac_read_pcm_frames_s16(decoder, frames, out); }, error);
        if (result && decoder->totalPCMFrameCount && result->Frames() != decoder->totalPCMFrameCount) {
            error = "truncated FLAC frames"; return {};
        }
        return result;
    }
    if (type == ".ogg") {
        auto scratch = std::make_unique<char[]>(Scratch);
        const stb_vorbis_alloc allocator{scratch.get(), static_cast<int>(Scratch)};
        int code = 0;
        auto *decoder = stb_vorbis_open_memory(input.data(), static_cast<int>(input.size()), &code, &allocator);
        if (!decoder) return {};
        std::unique_ptr<stb_vorbis, decltype(&stb_vorbis_close)> close(decoder, stb_vorbis_close);
        const auto info = stb_vorbis_get_info(decoder);
        auto result = Read(info.sample_rate, info.channels, max_bytes,
            [&](auto *out, auto frames) { return stb_vorbis_get_samples_short_interleaved(decoder, info.channels, out, frames * info.channels); }, error);
        if (stb_vorbis_get_error(decoder)) { error = "invalid Ogg audio stream"; return {}; }
        return result;
    }
    error = "unsupported audio type; use WAV, FLAC or Ogg Vorbis"; return {};
}
}
