#pragma once
#include <map>
#include "runtime.hpp"
#include "audio/pcm.hpp"
#include "audio/stream.hpp"
namespace audio { class Mixer; }
namespace mods {
// Game-thread owner. The shared mixer alone renders on the audio thread.
// Mixer must outlive this object. No global mixer registration on construction.
class AudioStreams {
public:
    explicit AudioStreams(audio::Mixer &mixer) : mixer_(mixer) {}
    ~AudioStreams();
    AudioStreams(const AudioStreams &) = delete;
    AudioStreams &operator=(const AudioStreams &) = delete;
    bool Register(std::string owner, std::string id, const audio::PcmTrack &track, std::string &error);
    std::string Play(std::string_view owner, std::string_view id, bool loop, double volume, double pan, std::string &error);
    bool Stop(std::string_view owner, std::string_view handle, std::string &error);
    void Retain(std::span<const std::string> owners);
    void Prune();
    void Clear();
    nlohmann::json State(std::string_view owner) const;
private:
    struct Sound { std::shared_ptr<const audio::PcmTrack> track; std::size_t bytes; };
    struct Voice { std::string owner, id; std::unique_ptr<audio::StreamChannel> channel; };
    audio::Mixer &mixer_;
    std::map<std::pair<std::string, std::string>, Sound> sounds_;
    std::map<std::string, Voice> voices_;
    std::uint64_t next_ = 1;
};
}
