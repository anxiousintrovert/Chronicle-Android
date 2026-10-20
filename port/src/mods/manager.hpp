#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "versions.hpp"

namespace mods {

using FileIndex = std::unordered_map<std::string, std::filesystem::path>;
bool Supports(std::string_view capability);

struct Quest {
    std::string id, title, description;
    int         goal = 1;
    int         map = -1; // -1 means only scripts advance this quest.
};

struct MeshReplacement {
    std::string texture;
    unsigned native_vertices = 0; // Zero matches every vertex count for this texture.
    std::filesystem::path source;
    bool Overlaps(const MeshReplacement &other) const {
        return texture == other.texture && (!native_vertices || !other.native_vertices || native_vertices == other.native_vertices);
    }
};

struct TextureReplacement {
    std::string name, original_sha256, source_sha256;
    std::filesystem::path source, package_root;
    std::string Key() const { return name + ":" + original_sha256; }
};

struct Package {
    std::filesystem::path                        root;
    std::string                                  id;
    std::string                                  name;
    std::string                                  version;
    std::string                                  save_migrations; // Validated JSON; empty means exact-version loading only.
    bool                                         enabled = false;
    std::vector<std::string>                     dependencies;
    std::map<std::string, VersionRequirement>    dependency_versions;
    std::vector<std::string>                     conflicts;
    std::vector<std::string>                     exclusive;
    std::vector<std::string>                     permissions;
    std::vector<Quest>                           quests;
    std::vector<MeshReplacement>                 meshes;
    std::vector<TextureReplacement>              textures;
    std::filesystem::path                        script;
    std::filesystem::path                        native_library;
    std::string                                  native_fingerprint;
    bool                                         native_trusted = false;
    std::map<std::string, std::filesystem::path> resources;
    std::map<std::string, std::filesystem::path> audio;
    std::map<std::string, std::filesystem::path> files;
    std::vector<std::string>                     problems;

    bool Active() const { return enabled && problems.empty(); }
};

// A startup snapshot. Resolve every package before publishing any replacement.
// No writes to packages or extracted game data; no scripts or native code execute.
class Manager {
public:
    void Scan(const std::vector<std::filesystem::path> &roots, const FileIndex &base,
              const std::map<std::string, bool> &enabled = {}, const std::map<std::string, std::string> &trusted = {});

    const std::vector<Package> &Packages() const { return packages_; }

    const std::vector<std::string> &Errors() const { return errors_; }

    const std::vector<std::string> &LoadOrder() const { return order_; }

    const FileIndex &Overrides() const { return overrides_; }

    const std::map<std::string,TextureReplacement> &Textures() const { return textures_; }
    std::string Report() const;

private:
    std::vector<Package>     packages_;
    std::vector<std::string> errors_;
    std::vector<std::string> order_;
    FileIndex                overrides_;
    std::map<std::string,TextureReplacement> textures_;
};

} // namespace mods
