#pragma once
#include "manager.hpp"
#include "mesh_asset.hpp"
#include <memory>
#include <span>
namespace mods {
// Decoded, immutable package content. No native IDs or GPU allocations are owned.
class MeshCatalog {
public:
    struct Definition {
        std::string owner;
        MeshReplacement target;
        MeshAsset asset;
        std::uintmax_t bytes = 0;
    };
    struct Match {
        std::shared_ptr<const Definition> first, conflict;
        bool Unique() const noexcept { return first && !conflict; }
    };
    // Startup only. Inactive packages do no I/O. Failed loads retain the previous
    // complete owner snapshot; callers retire failed owners before construction.
    bool LoadPackage(const Package &package, std::string &error);
    Match Select(std::span<const std::string_view> textures, unsigned native_vertices) const noexcept;
    void Retain(std::span<const std::string> owners);
    void Clear() noexcept { definitions_.clear(); }
    std::size_t Size() const noexcept { return definitions_.size(); }
private:
    std::vector<std::shared_ptr<const Definition>> definitions_;
};
}
