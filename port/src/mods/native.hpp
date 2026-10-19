#pragma once
#include "native_plugin.h"

#include <memory>
#include <string_view>

#include "manager.hpp"

namespace mods {
std::string Sha256(std::string_view bytes);
std::string NativeFingerprint(const Package &package);

class NativeLibrary {
public:
    NativeLibrary();
    ~NativeLibrary();
    NativeLibrary(const NativeLibrary &) = delete;
    NativeLibrary &operator=(const NativeLibrary &) = delete;
    bool           Start(const Package &package, ChronicleModHostV1 host, ChronicleModHostV2 host2, std::string &error);
    bool           Tick();
    bool           WantsEvent(uint32_t event) const;
    bool           Event(uint32_t event, const char *world_json);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace mods
