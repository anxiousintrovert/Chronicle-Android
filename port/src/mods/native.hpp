#pragma once
#include "native_plugin.h"

#include <memory>
#include <span>
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
    NativeLibrary                            &operator=(const NativeLibrary &) = delete;
    bool                                      Start(const Package &package, ChronicleModHostV1 host, ChronicleModHostV2 host2, std::string &error);
    bool                                      Tick();
    bool                                      WantsEvent(uint32_t event) const;
    bool                                      Event(uint32_t event, const char *world_json);
    std::span<const ChronicleModMenuActionV1> Actions() const;
    bool                                      RunAction(std::string_view id, const char *world_json);
    std::span<const ChronicleModServiceV1>    Services() const;
    bool                                      InvokeService(std::string_view name, bool command, const char *arguments, const char *caller,
                                                            std::string &result, std::string &error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace mods
