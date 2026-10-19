#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace mods {

using FileIndex = std::unordered_map<std::string, std::filesystem::path>;
bool Supports(std::string_view capability);

struct Quest {
    std::string id, title, description;
    int         goal = 1;
    int         map = -1; // -1 means only scripts advance this quest.
};

struct Package {
    std::filesystem::path                        root;
    std::string                                  id;
    std::string                                  name;
    std::string                                  version;
    bool                                         enabled = true;
    std::vector<std::string>                     dependencies;
    std::vector<std::string>                     conflicts;
    std::vector<std::string>                     exclusive;
    std::vector<std::string>                     permissions;
    std::vector<Quest>                           quests;
    std::filesystem::path                        script;
    std::map<std::string, std::filesystem::path> files;
    std::vector<std::string>                     problems;

    bool Active() const { return enabled && problems.empty(); }
};

// A startup snapshot. Resolve every package before publishing any replacement.
// No writes to packages or extracted game data; no scripts or native code execute.
class Manager {
public:
    void Scan(const std::vector<std::filesystem::path> &roots, const FileIndex &base,
              const std::map<std::string, bool> &enabled = {});

    const std::vector<Package> &Packages() const { return packages_; }

    const std::vector<std::string> &Errors() const { return errors_; }

    const std::vector<std::string> &LoadOrder() const { return order_; }

    const FileIndex &Overrides() const { return overrides_; }

    std::string Report() const;

private:
    std::vector<Package>     packages_;
    std::vector<std::string> errors_;
    std::vector<std::string> order_;
    FileIndex                overrides_;
};

} // namespace mods
