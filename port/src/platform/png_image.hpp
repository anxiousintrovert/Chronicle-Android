#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>
#include <string>

// Chronicle localization and UI accept standard PNG color types and normalize
// them to RGBA, including RGB screenshots from the shared renderer.
bool PngLoadRgba(const std::filesystem::path &file, int &width, int &height, std::vector<std::uint8_t> &rgba);

// Opt-in limits for package images; existing localization/UI loading is unchanged.
struct PngLimits {
    std::size_t encoded_bytes = 32u * 1024u * 1024u;
    unsigned dimension = 8192;
    std::size_t pixels = 16u * 1024u * 1024u;
};
// On failure all image outputs are unchanged and error explains the rejection.
bool PngLoadRgbaBounded(const std::filesystem::path &file, const PngLimits &limits,
                        int &width, int &height, std::vector<std::uint8_t> &rgba, std::string &error);
