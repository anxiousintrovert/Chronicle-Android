#pragma once
#include "mesh_skin.hpp"
#include <string>
namespace mods {
bool AttachMeshSkin(const void *visual, const void *model, std::span<const MeshSkinBinding::Point> source,
                    std::span<const MeshSkinBinding::Point> replacement, std::string &error);
void DetachMeshSkin(const void *visual) noexcept;
std::span<const MeshSkinBinding::Vertex> ReadMeshSkin(const void *visual) noexcept;
std::string_view MeshSkinError(const void *visual) noexcept;
std::size_t MeshSkinBindingCount() noexcept;
void BeginNativeMeshSkin(const void *model, const void *rest_vertices, std::size_t count) noexcept;
bool NativeMeshSkinActive() noexcept;
void ApplyNativeMeshBone(std::uint32_t source_vertex,const float base[4][4],const float bone[4][4],
                         const float inverse[4][4],float weight) noexcept;
}
