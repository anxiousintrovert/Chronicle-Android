#include "native.hpp"

#include <SDL3/SDL_loadso.h>

#include <array>
#include <bit>
#include <fstream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace mods {
std::string Sha256(std::string_view bytes) {
    constexpr uint32_t k[] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::array<uint32_t, 8> h{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::vector<uint8_t>    data(bytes.begin(), bytes.end());
    const uint64_t          bits = static_cast<uint64_t>(data.size()) * 8;
    data.push_back(0x80);
    while (data.size() % 64 != 56) {
        data.push_back(0);
    }
    for (int i = 7; i >= 0; --i) {
        data.push_back(static_cast<uint8_t>(bits >> (i * 8)));
    }
    for (size_t offset = 0; offset < data.size(); offset += 64) {
        uint32_t w[64]{};
        for (int i = 0; i < 16; ++i) {
            for (int j = 0; j < 4; ++j) {
                w[i] = (w[i] << 8) | data[offset + i * 4 + j];
            }
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t a = std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t b = std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + a + w[i - 7] + b;
        }
        auto s = h;
        for (int i = 0; i < 64; ++i) {
            const uint32_t t1 = s[7] + (std::rotr(s[4], 6) ^ std::rotr(s[4], 11) ^ std::rotr(s[4], 25)) + ((s[4] & s[5]) ^ (~s[4] & s[6])) + k[i] + w[i];
            const uint32_t t2 = (std::rotr(s[0], 2) ^ std::rotr(s[0], 13) ^ std::rotr(s[0], 22)) + ((s[0] & s[1]) ^ (s[0] & s[2]) ^ (s[1] & s[2]));
            s = {t1 + t2, s[0], s[1], s[2], s[3] + t1, s[4], s[5], s[6]};
        }
        for (int i = 0; i < 8; ++i) {
            h[i] += s[i];
        }
    }
    std::string result;
    for (uint32_t word : h) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            result.push_back("0123456789abcdef"[(word >> shift) & 15]);
        }
    }
    return result;
}

std::string NativeFingerprint(const Package &p) {
    if (p.native_library.empty()) {
        return {};
    }
    const auto file = std::filesystem::canonical(p.native_library);
    const auto root = std::filesystem::canonical(p.root);
    const auto rel = file.lexically_relative(root);
    if (rel.empty() || rel.is_absolute() || *rel.begin() == ".." || !std::filesystem::is_regular_file(file)) {
        throw std::runtime_error("native library escapes package");
    }
    const auto size = std::filesystem::file_size(file);
    if (!size || size > 64 * 1024 * 1024) {
        throw std::runtime_error("native library must be 1 byte to 64 MiB");
    }
    std::ifstream input(file, std::ios::binary);
    std::string   bytes(static_cast<size_t>(size), '\0');
    if (!input.read(bytes.data(), bytes.size()) || input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("native library changed while reading");
    }
    // Bind approval to package identity, version, declared permissions and path as
    // well as all library bytes. Changing host authority requires new approval.
    std::string metadata = p.id + '\n' + p.version + '\n' + root.generic_string() + '\n' + rel.generic_string();
    for (const auto &permission : p.permissions) {
        metadata += '\n' + permission;
    }
    return Sha256(metadata + '\n' + Sha256(bytes));
}

struct NativeLibrary::Impl {
#ifdef _WIN32
    HMODULE library = nullptr;
#else
    SDL_SharedObject *library = nullptr;
#endif
    const ChronicleModPluginV1 *plugin = nullptr;
    const ChronicleModPluginV2 *plugin2 = nullptr;
    const ChronicleModMenuV1   *menu = nullptr;
    ChronicleModHostV1          host{};
    ChronicleModHostV2          host2{};
    void                       *state = nullptr;
    bool                        started = false;

    ~Impl() {
        if (started && plugin2) {
            plugin2->stop(&host2, state);
        } else if (started && plugin) {
            plugin->stop(&host, state);
        }
        if (library) {
#ifdef _WIN32
            FreeLibrary(library);
#else
            SDL_UnloadObject(library);
#endif
        }
    }
};

NativeLibrary::NativeLibrary() : impl_(std::make_unique<Impl>()) {}

NativeLibrary::~NativeLibrary() = default;

bool NativeLibrary::Start(const Package &p, ChronicleModHostV1 host, ChronicleModHostV2 host2, std::string &error) {
    try {
        if (!p.Active() || !p.native_trusted || p.native_fingerprint.empty() || NativeFingerprint(p) != p.native_fingerprint) {
            throw std::runtime_error("native library is disabled, untrusted, or changed");
        }
#ifdef _WIN32
        impl_->library = LoadLibraryExW(p.native_library.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
#else
        const auto utf8 = p.native_library.u8string();
        impl_->library = SDL_LoadObject(reinterpret_cast<const char *>(utf8.c_str()));
#endif
        if (!impl_->library) {
            throw std::runtime_error("native library could not be loaded for this platform");
        }
#ifdef _WIN32
        auto entry2 = reinterpret_cast<ChronicleModEntryV2>(GetProcAddress(impl_->library, "ChronicleMod_GetPluginV2"));
        auto menu_entry = reinterpret_cast<ChronicleModMenuEntryV1>(GetProcAddress(impl_->library, "ChronicleMod_GetMenuV1"));
        auto entry = reinterpret_cast<ChronicleModEntryV1>(GetProcAddress(impl_->library, "ChronicleMod_GetPluginV1"));
#else
        auto entry2 = reinterpret_cast<ChronicleModEntryV2>(SDL_LoadFunction(impl_->library, "ChronicleMod_GetPluginV2"));
        auto menu_entry = reinterpret_cast<ChronicleModMenuEntryV1>(SDL_LoadFunction(impl_->library, "ChronicleMod_GetMenuV1"));
        auto entry = reinterpret_cast<ChronicleModEntryV1>(SDL_LoadFunction(impl_->library, "ChronicleMod_GetPluginV1"));
#endif
        if (entry2) {
            impl_->plugin2 = entry2();
            const auto *p2 = impl_->plugin2;
            if (!p2 || p2->size < sizeof(ChronicleModPluginV2) || p2->abi != 2 || !p2->id || p.id != p2->id ||
                !p2->start || !p2->tick || !p2->stop || (p2->event_mask & ~CHRONICLE_EVENT_ALL) || (p2->event_mask && !p2->event)) {
                throw std::runtime_error("incompatible native ABI 2 descriptor");
            }
            impl_->host2 = host2;
            if (menu_entry) {
                const auto *menu = menu_entry();
                if (!menu || menu->size < sizeof(ChronicleModMenuV1) || menu->abi != 1 || !menu->id || p.id != menu->id ||
                    menu->count > 32 || (menu->count && !menu->actions)) {
                    throw std::runtime_error("incompatible native menu descriptor");
                }
                for (uint32_t i = 0; i < menu->count; ++i) {
                    const auto &action = menu->actions[i];
                    // Arrays use this exact stride; a future entry layout needs
                    // a new extension version rather than larger array elements.
                    if (action.size != sizeof(ChronicleModMenuActionV1) || !action.id || !action.title || !action.description || !action.run) {
                        throw std::runtime_error("invalid native menu action descriptor");
                    }
                }
                impl_->menu = menu;
            }
            impl_->started = true;
            if (!p2->start(&impl_->host2, &impl_->state)) {
                throw std::runtime_error("native ABI 2 start callback failed");
            }
            return true;
        }
        if (menu_entry) {
            throw std::runtime_error("native menu actions require plugin ABI 2");
        }
        if (!entry) {
            throw std::runtime_error("missing ChronicleMod_GetPluginV1 or ChronicleMod_GetPluginV2");
        }
        impl_->plugin = entry();
        const auto *plugin = impl_->plugin;
        if (!plugin || plugin->size < sizeof(ChronicleModPluginV1) || plugin->abi != 1 || !plugin->id || p.id != plugin->id ||
            !plugin->start || !plugin->tick || !plugin->stop) {
            throw std::runtime_error("incompatible native descriptor");
        }
        impl_->host = host;
        impl_->started = true; // stop also releases partial initialization on failure.
        if (!plugin->start(&impl_->host, &impl_->state)) {
            throw std::runtime_error("native start callback failed");
        }
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        impl_ = std::make_unique<Impl>();
        return false;
    }
}

bool NativeLibrary::Tick() {
    return impl_->started && (impl_->plugin2 ? impl_->plugin2->tick(&impl_->host2, impl_->state) : impl_->plugin->tick(&impl_->host, impl_->state));
}

bool NativeLibrary::WantsEvent(uint32_t event) const {
    return impl_->started && impl_->plugin2 && (impl_->plugin2->event_mask & event) != 0;
}

bool NativeLibrary::Event(uint32_t event, const char *world_json) {
    return !WantsEvent(event) || impl_->plugin2->event(&impl_->host2, impl_->state, event, world_json);
}

std::span<const ChronicleModMenuActionV1> NativeLibrary::Actions() const {
    if (!impl_->started || !impl_->menu) {
        return {};
    }
    return {impl_->menu->actions, impl_->menu->count};
}

bool NativeLibrary::RunAction(std::string_view id, const char *world_json) {
    for (const auto &action : Actions()) {
        if (id == action.id) {
            return action.run(&impl_->host2, impl_->state, world_json);
        }
    }
    return false;
}
} // namespace mods
