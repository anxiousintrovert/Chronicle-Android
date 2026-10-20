#include "music_pack.hpp"
#include "../audio_retail.hpp"
#include <map>
#include <algorithm>
#include <charconv>
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
bool ParseMusicConfig(std::string_view text, MusicReverb &result, std::string &error) {
    if (text.size() > 65536) { error = "music config exceeds 64 KiB"; return false; }
    MusicReverb staged;
    const auto nul = text.find('\0');
    if (nul != std::string_view::npos) {
        if (text.find_first_not_of('\0', nul) != std::string_view::npos) {
            error = "music config contains data after terminator"; return false;
        }
        text = text.substr(0, nul);
    }
    const auto space = [&] {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n'))
            text.remove_prefix(1);
    };
    const auto integer = [&](int &value) {
        space();
        if (text.empty()) return false;
        if (text.front() == '+') { text.remove_prefix(1); if (text.empty() || text.front() < '0' || text.front() > '9') return false; }
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc()) return false;
        text.remove_prefix(parsed.ptr - text.data()); return true;
    };
    for (;;) {
        space(); if (text.empty()) break;
        const auto end = text.find_first_of(" \t\r\n");
        if (end == std::string_view::npos) { error = "incomplete music config command"; return false; }
        const auto command = text.substr(0, end); text.remove_prefix(end);
        if (command != "REVERBE" && command != "TABLE") { error = "unknown music config command"; return false; }
        int first, second;
        if (!integer(first)) { error = "invalid first music config integer"; return false; }
        space();
        if (text.empty() || text.front() != ',') { error = "music config requires comma-separated integers"; return false; }
        text.remove_prefix(1);
        if (!integer(second) || (!text.empty() && text.front() != ' ' && text.front() != '\t' && text.front() != '\r' && text.front() != '\n')) {
            error = "invalid second music config integer"; return false;
        }
        // TABLE is parsed by retail but SetBGMFile consumes only reverb.
        // Match the official mixer's limits without evaluating native script.
        if (command == "REVERBE") staged = {std::clamp(first, 0, 9), std::clamp(second, 0, 127)};
    }
    result = staged; error.clear(); return true;
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
            MusicReverb reverb;
            if (!ParseMusicConfig({reinterpret_cast<const char *>(cfg->second.data()), cfg->second.size()}, reverb, error))
                throw std::runtime_error(error);
            result->reverb = reverb;
        }
        error.clear(); return result;
    } catch (const std::exception &failure) { error = failure.what(); return {}; }
}
}
