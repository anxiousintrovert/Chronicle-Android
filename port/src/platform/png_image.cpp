#include "png_image.hpp"

#include <fstream>
#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <memory>

// Reuse Kazarov's decoder implementation in platform/glyphs.cpp. Its published
// texture catalog requires RGBA8 files, but Chronicle's existing UI/localization
// contract also accepts RGB, palette and grayscale PNGs.
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

bool PngLoadRgba(const std::filesystem::path &file, int &width, int &height, std::vector<std::uint8_t> &rgba) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return false;
    }
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (bytes.empty() || bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    int                                                        decoded_width = 0, decoded_height = 0, channels = 0;
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &decoded_width, &decoded_height, &channels, 4),
        stbi_image_free);
    if (!pixels) {
        return false;
    }
    rgba.assign(pixels.get(), pixels.get() + static_cast<std::size_t>(decoded_width) * decoded_height * 4);
    width = decoded_width;
    height = decoded_height;
    return true;
}

bool PngLoadRgbaBounded(const std::filesystem::path &file, const PngLimits &limits,
                        int &width, int &height, std::vector<std::uint8_t> &rgba, std::string &error) {
    error.clear();
    try {
        if(!std::filesystem::is_regular_file(file)) {error="PNG source is not a regular file";return false;}
        const auto size=std::filesystem::file_size(file);
        if(size<33||size>limits.encoded_bytes||size>static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            error="PNG encoded size exceeds limits or is truncated";return false;
        }
        std::ifstream input(file,std::ios::binary);
        std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
        if(!input.read(reinterpret_cast<char *>(bytes.data()),static_cast<std::streamsize>(size))||input.peek()!=std::char_traits<char>::eof()) {
            error="PNG source changed or cannot be read";return false;
        }
        constexpr std::array<unsigned char,8> signature{137,80,78,71,13,10,26,10};
        if(!std::equal(signature.begin(),signature.end(),bytes.begin())) {error="Replacement image must be PNG";return false;}
        int w=0,h=0,channels=0;
        if(!stbi_info_from_memory(bytes.data(),static_cast<int>(bytes.size()),&w,&h,&channels)||w<=0||h<=0) {
            error="Invalid PNG header";return false;
        }
        if(static_cast<unsigned>(w)>limits.dimension||static_cast<unsigned>(h)>limits.dimension||
           static_cast<std::size_t>(w)>limits.pixels/static_cast<unsigned>(h)||
           static_cast<std::size_t>(w)>std::numeric_limits<std::size_t>::max()/4/static_cast<unsigned>(h)) {
            error="PNG dimensions or decoded pixel count exceed limits";return false;
        }
        int decoded_w=0,decoded_h=0;
        std::unique_ptr<unsigned char,decltype(&stbi_image_free)> decoded(
            stbi_load_from_memory(bytes.data(),static_cast<int>(bytes.size()),&decoded_w,&decoded_h,&channels,4),stbi_image_free);
        if(!decoded||decoded_w!=w||decoded_h!=h) {error="PNG pixel data cannot be decoded";return false;}
        std::vector<std::uint8_t> next(decoded.get(),decoded.get()+static_cast<std::size_t>(w)*h*4);
        rgba.swap(next);width=w;height=h;return true;
    } catch(const std::exception &failure) {error=std::string("Cannot load PNG: ")+failure.what();return false;}
}
