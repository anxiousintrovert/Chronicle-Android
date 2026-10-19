#include "session.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <set>

#include "native.hpp"
#include "platform/files.hpp"

namespace mods {
namespace fs = std::filesystem;
using Json = nlohmann::json;

void Session::Start(const std::vector<fs::path> &roots, const FileIndex &base, const fs::path &save) {
    roots_ = roots;
    base_ = base;
    save_ = save;
    started_once_ = true;
    enabled_.clear();
    trusted_.clear();
    error_.clear();
    profile_valid_ = true;
    try {
        const auto path = save / "mods-profile.json";
        if (fs::exists(path)) {
            if (!fs::is_regular_file(path) || fs::file_size(path) > 256 * 1024) {
                throw std::runtime_error("invalid profile file");
            }
            std::ifstream input(path);
            const Json    profile = Json::parse(input);
            if (profile.at("schema_version") != 1 || !profile.at("enabled").is_object()) {
                throw std::runtime_error("unsupported profile format");
            }
            for (const auto &[id, value] : profile.at("enabled").items()) {
                if (!value.is_boolean()) {
                    throw std::runtime_error("profile enable states must be boolean");
                }
                enabled_[id] = value.get<bool>();
            }
            if (profile.contains("native_trust")) {
                if (!profile["native_trust"].is_object()) {
                    throw std::runtime_error("invalid native trust profile");
                }
                for (const auto &[id, value] : profile["native_trust"].items()) {
                    if (!value.is_string() || value.get_ref<const std::string &>().size() != 64) {
                        throw std::runtime_error("invalid native fingerprint");
                    }
                    trusted_[id] = value.get<std::string>();
                }
            }
        }
    } catch (const std::exception &e) {
        profile_valid_ = false;
        error_ = std::string("Cannot read mods-profile.json: ") + e.what() + ". Repair it and restart; no mods were activated.";
    }
    preview_.Scan(roots_, base_, enabled_, trusted_);
    if (!profile_valid_) {
        for (const auto &p : preview_.Packages()) {
            enabled_[p.id] = false;
        }
        trusted_.clear();
        preview_.Scan(roots_, base_, enabled_, trusted_);
    }
    // Materialize defaults so toggling back to the startup state clears the restart notice.
    for (const auto &p : preview_.Packages()) {
        enabled_.try_emplace(p.id, p.enabled);
    }
    started_ = enabled_;
    started_trust_ = trusted_;
    active_ = preview_;
}

void Session::Refresh() {
    preview_.Scan(roots_, base_, enabled_, trusted_);
}

bool Session::SetEnabled(const std::string &id, bool enabled) {
    if (!profile_valid_) {
        return false;
    }
    std::size_t matches = 0;
    for (const auto &p : preview_.Packages()) {
        if (p.id == id && !id.empty()) {
            ++matches;
        }
    }
    if (matches != 1) {
        error_ = "This package has no unique ID. Fix its manifest or duplicate folders first.";
        return false;
    }
    auto next = enabled_;
    next[id] = enabled;
    if (!WriteProfile(next, trusted_)) {
        return false;
    }
    enabled_ = std::move(next);
    error_.clear();
    Refresh();
    return true;
}

bool Session::SetTrusted(const std::string &id, const std::string &fingerprint, bool trusted) {
    if (!profile_valid_) {
        return false;
    }
    const Package *package = nullptr;
    for (const auto &p : preview_.Packages()) {
        if (p.id == id) {
            if (package) {
                error_ = "Duplicate package ID cannot be trusted";
                return false;
            }
            package = &p;
        }
    }
    if (!package || package->native_library.empty()) {
        error_ = "No native library selected";
        return false;
    }
    try {
        if (trusted && (fingerprint.empty() || fingerprint != package->native_fingerprint || NativeFingerprint(*package) != fingerprint)) {
            error_ = "Native library changed. Rescan and review it again.";
            return false;
        }
    } catch (const std::exception &e) {
        error_ = e.what();
        return false;
    }
    auto next = trusted_;
    if (trusted) {
        next[id] = fingerprint;
    } else {
        next.erase(id);
    }
    if (!WriteProfile(enabled_, next)) {
        return false;
    }
    trusted_ = std::move(next);
    error_.clear();
    Refresh();
    return true;
}

bool Session::WriteProfile(const std::map<std::string, bool> &enabled, const std::map<std::string, std::string> &trusted) {
    const std::string data = Json{
                                 {"schema_version", 1      },
                                 {"enabled",        enabled},
                                 {"native_trust",   trusted}
    }
                                 .dump(2) +
                             '\n';
    std::error_code   ec;
    fs::create_directories(save_, ec);
    if (ec || FilesWrite(save_ / "mods-profile.json", data.data(), data.size(), true) != FilesResult::kWritten) {
        error_ = "Could not save mods-profile.json. The selection was not changed.";
        return false;
    }
    return true;
}

Session &Shared() {
    static Session session;
    return session;
}
} // namespace mods
