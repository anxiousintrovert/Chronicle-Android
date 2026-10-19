#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace mods {
struct VersionRequirement {
    std::string exact, minimum, before;
    std::string Describe() const;
    std::string Validate() const;
    bool        Accepts(std::string_view version) const;
};

// SemVer precedence; no integer conversion/overflow, build metadata ignored.
std::optional<int> CompareVersions(std::string_view left, std::string_view right);
} // namespace mods
