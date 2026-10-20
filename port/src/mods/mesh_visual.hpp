#pragma once
#include "mesh_asset.hpp"
#include "mesh_skin.hpp"
#include "draw3d.hpp"
namespace mods {
struct PreparedMeshVisual {
    Draw3DVisual visual;
    std::vector<MeshSkinBinding::Point> source_rest, replacement_rest;
    std::vector<std::uint32_t> source_vertices;
};
// Detached CPU data only: no GPU handle is created or inherited.
PreparedMeshVisual PrepareMeshVisual(const Draw3DVisual &source,const MeshAsset &asset);
// Supply a complete native-skin pose, or an empty pose for source displacement
// (morph-only) updates. Validates before publishing; allocates nothing.
bool RefreshMeshVisual(PreparedMeshVisual &prepared,const Draw3DVisual &source,
                       std::span<const MeshSkinBinding::Vertex> pose={}) noexcept;
}
