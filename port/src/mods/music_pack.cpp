#include "music_pack.hpp"
#include "../audio_retail.hpp"
#include <map>
#include <cstring>
#include <stdexcept>

namespace mods {
namespace {
std::string Fold(std::string value) {
    for (auto &c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}
std::uint32_t Word(std::span<const std::uint8_t> bytes, std::size_t at) {
    return std::uint32_t(bytes[at]) | (std::uint32_t(bytes[at+1]) << 8) |
           (std::uint32_t(bytes[at+2]) << 16) | (std::uint32_t(bytes[at+3]) << 24);
}
}
std::shared_ptr<PreparedMusicPack> PrepareMusicPack(std::span<const std::uint8_t> bytes, int set, std::string &error) {
    try {
        if (set < 0 || set > 255 || bytes.size() < 76 || bytes.size() > 128 * 1024 * 1024)
            throw std::runtime_error("invalid BGM set or archive size");
        std::map<std::string, std::span<const std::uint8_t>> members;
        std::size_t at = 0;
        for (;;) {
            if (bytes.size() - at < 76) throw std::runtime_error("missing music pack terminator");
            if (!bytes[at]) break;
            if (members.size() >= 4096) throw std::runtime_error("too many music pack members");
            const auto *end = static_cast<const std::uint8_t *>(std::memchr(bytes.data() + at, 0, 64));
            if (!end) throw std::runtime_error("unterminated music pack member name");
            const auto offset = Word(bytes, at+64), size = Word(bytes, at+68), next = Word(bytes, at+72);
            if (offset < 76 || offset > bytes.size()-at || size > bytes.size()-at-offset || next < 76 || next > bytes.size()-at)
                throw std::runtime_error("music pack member range is invalid");
            const std::string name(reinterpret_cast<const char *>(bytes.data()+at), end-(bytes.data()+at));
            if (!members.emplace(Fold(name), bytes.subspan(at+offset, size)).second)
                throw std::runtime_error("duplicate music pack member");
            at += next;
        }
        const auto find = [&](const std::string &name) {
            const auto it = members.find(Fold(name));
            if (it == members.end()) throw std::runtime_error("missing music pack member: " + name);
            return it->second;
        };
        const auto list = find("bgm" + std::to_string(set) + ".txt");
        if (list.empty() || list.size() > 65536) throw std::runtime_error("invalid music list size");
        const std::string text(reinterpret_cast<const char *>(list.data()), strnlen(reinterpret_cast<const char *>(list.data()), list.size()));
        auto result = std::make_shared<PreparedMusicPack>();
        for (std::size_t start = 0; start < text.size();) {
            auto end = text.find('\n', start); if (end == std::string::npos) end = text.size();
            const auto line = text.substr(start, end-start); start = end+1;
            if (line.size() < 9) continue; // same ignored placeholder lines as the native loader
            const auto name = line.substr(0, 9), extension = name.substr(6);
            const auto slot = name[5];
            if (result->entries.size() >= 64) throw std::runtime_error("too many BGM load records");
            if (extension == ".hd" && std::string_view("acegimqs").find(slot) != std::string_view::npos) {
                auto bank = chronicle_audio::CreateBank(find(name), find(name.substr(0,7) + "bd"));
                if (!bank) throw std::runtime_error("cannot decode music bank: " + name);
                result->entries.push_back({slot, name, std::move(bank), {}});
            } else if (extension == ".sq" && std::string_view("aei").find(slot) != std::string_view::npos) {
                auto sequence = chronicle_audio::CreateSequence(find(name));
                if (!sequence || sequence->Songs().empty()) throw std::runtime_error("cannot decode music sequence: " + name);
                result->entries.push_back({slot, name, {}, std::move(sequence)});
            }
        }
        if (result->entries.empty()) throw std::runtime_error("music archive contains no loadable records");
        const auto cfg = members.find("bgm" + std::to_string(set) + ".cfg");
        if (cfg != members.end()) {
            if (cfg->second.size() > 65536) throw std::runtime_error("music config exceeds 64 KiB");
            result->config.assign(reinterpret_cast<const char *>(cfg->second.data()), cfg->second.size());
        }
        error.clear(); return result;
    } catch (const std::exception &failure) { error = failure.what(); return {}; }
}
}
