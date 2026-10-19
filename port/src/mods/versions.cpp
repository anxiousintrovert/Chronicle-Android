#include "versions.hpp"

#include <array>
#include <vector>

namespace mods {
namespace {
struct Version {
    std::array<std::string_view, 3> core;
    std::vector<std::string_view>   pre;
};

bool Digits(std::string_view s) {
    if (s.empty()) {
        return false;
    }
    for (char c : s) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

bool Numeric(std::string_view s) { return Digits(s) && (s.size() == 1 || s[0] != '0'); }

bool Identifiers(std::string_view s, bool prerelease, std::vector<std::string_view> &parts) {
    while (true) {
        auto pos = s.find('.');
        auto part = s.substr(0, pos);
        if (part.empty()) {
            return false;
        }
        for (char c : part) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-')) {
                return false;
            }
        }
        if (prerelease && Digits(part) && !Numeric(part)) {
            return false;
        }
        parts.push_back(part);
        if (pos == s.npos) {
            return true;
        }
        s.remove_prefix(pos + 1);
    }
}

std::optional<Version> Parse(std::string_view text) {
    if (text.empty() || text.size() > 256) {
        return {};
    }
    auto plus = text.find('+');
    if (plus != text.npos) {
        std::vector<std::string_view> build;
        if (!Identifiers(text.substr(plus + 1), false, build)) {
            return {};
        }
        text = text.substr(0, plus);
    }
    Version result;
    auto    dash = text.find('-');
    if (dash != text.npos) {
        if (!Identifiers(text.substr(dash + 1), true, result.pre)) {
            return {};
        }
        text = text.substr(0, dash);
    }
    for (unsigned i = 0; i < 3; ++i) {
        auto pos = text.find('.');
        result.core[i] = text.substr(0, pos);
        if (!Numeric(result.core[i]) || (i < 2 && pos == text.npos) || (i == 2 && pos != text.npos)) {
            return {};
        }
        if (pos != text.npos) {
            text.remove_prefix(pos + 1);
        }
    }
    return result;
}

int Lexical(std::string_view a, std::string_view b) { return a < b ? -1 : a > b ? 1
                                                                                : 0; }

int Number(std::string_view a, std::string_view b) { return a.size() < b.size() ? -1 : a.size() > b.size() ? 1
                                                                                                           : Lexical(a, b); }
} // namespace

std::optional<int> CompareVersions(std::string_view left, std::string_view right) {
    auto a = Parse(left), b = Parse(right);
    if (!a || !b) {
        return {};
    }
    for (unsigned i = 0; i < 3; ++i) {
        if (int value = Number(a->core[i], b->core[i])) {
            return value;
        }
    }
    if (a->pre.empty() != b->pre.empty()) {
        return a->pre.empty() ? 1 : -1;
    }
    for (size_t i = 0; i < a->pre.size() && i < b->pre.size(); ++i) {
        bool an = Digits(a->pre[i]), bn = Digits(b->pre[i]);
        int  value = an != bn ? (an ? -1 : 1) : an ? Number(a->pre[i], b->pre[i])
                                                   : Lexical(a->pre[i], b->pre[i]);
        if (value) {
            return value;
        }
    }
    return a->pre.size() < b->pre.size() ? -1 : a->pre.size() > b->pre.size() ? 1
                                                                              : 0;
}

std::string VersionRequirement::Validate() const {
    if (!exact.empty()) {
        return minimum.empty() && before.empty() ? "" : "exact version cannot be combined with range bounds";
    }
    if (minimum.empty() && before.empty()) {
        return "version requirement needs exact, minimum or before";
    }
    if ((!minimum.empty() && !Parse(minimum)) || (!before.empty() && !Parse(before))) {
        return "version bounds require valid SemVer";
    }
    if (!minimum.empty() && !before.empty() && *CompareVersions(minimum, before) >= 0) {
        return "version range is empty";
    }
    return {};
}

bool VersionRequirement::Accepts(std::string_view version) const {
    if (!Validate().empty()) {
        return false;
    }
    if (!exact.empty()) {
        return version == exact;
    }
    if (!minimum.empty()) {
        auto cmp = CompareVersions(version, minimum);
        if (!cmp || *cmp < 0) {
            return false;
        }
    }
    if (!before.empty()) {
        auto cmp = CompareVersions(version, before);
        if (!cmp || *cmp >= 0) {
            return false;
        }
    }
    return true;
}

std::string VersionRequirement::Describe() const {
    if (!exact.empty()) {
        return "exact " + exact;
    }
    return (minimum.empty() ? "" : ">=" + minimum) + (minimum.empty() || before.empty() ? "" : " ") + (before.empty() ? "" : "<" + before);
}
} // namespace mods
