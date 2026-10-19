#include "save.hpp"

#include <cstdint>
#include <fstream>
#include <string_view>

#include "runtime.hpp"

namespace mods {
namespace {
constexpr std::string_view marker = "\nCHRONICLE_MODS_V1\n";
constexpr std::size_t      limit = 4 * 1024 * 1024;

std::uint32_t Checksum(std::string_view bytes) {
    std::uint32_t crc = 0xffffffffu;
    for (unsigned char c : bytes) {
        crc ^= c;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}
} // namespace

FilesResult WriteSave(const std::filesystem::path &path, const void *native, std::size_t size, bool replace) {
    const std::string state = Scripts().SaveState();
    if (state.empty()) {
        return FilesWrite(path, native, size, replace);
    }
    if (state.size() > limit) {
        return FilesResult::kFailed;
    }
    std::string image(static_cast<const char *>(native), size);
    image += marker;
    image += state;
    const auto crc = Checksum(image);
    for (int byte = 0; byte < 4; ++byte) {
        image.push_back(static_cast<char>((crc >> (byte * 8)) & 255u));
    }
    // Native image and namespaced mod state are one atomic file replacement.
    return FilesWrite(path, image.data(), image.size(), replace);
}

bool LoadSaveState(const std::filesystem::path &path, std::size_t native_size, std::string &error) {
    std::error_code ec;
    const auto      size = std::filesystem::file_size(path, ec);
    if (ec || size < native_size || size - native_size > limit + marker.size() + 4) {
        error = "invalid mod save length";
        return false;
    }
    if (size == native_size) {
        Scripts().ResetState();
        return true;
    }
    std::ifstream input(path, std::ios::binary);
    std::string   image(static_cast<std::size_t>(size), '\0');
    input.read(image.data(), static_cast<std::streamsize>(image.size()));
    if (!input || size - native_size < marker.size() + 4 || std::string_view(image).substr(native_size, marker.size()) != marker) {
        error = "unknown or damaged mod save extension";
        return false;
    }
    std::uint32_t stored = 0;
    for (int byte = 0; byte < 4; ++byte) {
        stored |= static_cast<std::uint32_t>(static_cast<unsigned char>(image[image.size() - 4 + byte])) << (byte * 8);
    }
    if (stored != Checksum(std::string_view(image).substr(0, image.size() - 4))) {
        error = "mod save checksum mismatch";
        return false;
    }
    return Scripts().LoadState(image.substr(native_size + marker.size(), image.size() - native_size - marker.size() - 4), error);
}
} // namespace mods
