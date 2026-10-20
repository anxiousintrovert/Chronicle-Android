#pragma once
#include "mesh_visual.hpp"
#include <string>
class CVisualVu1;
namespace mods {
bool AttachReplacementVisual(const void *block,const void *model,std::span<const MeshSkinBinding::Point> native_rest,
                             std::span<const std::uint32_t> flat_native_indices,const MeshAsset &asset,std::string &error);
// Returns false without touching output when this visual has no healthy replacement.
// Bounds conservatively include native corners and both published replacement poses.
bool ReplacementVisualCorners(CVisualVu1 &visual, const float native[8][4], float output[8][4]) noexcept;
void DetachReplacementVisual(const Draw3DVisual *source);
void RefreshReplacementVisual(const Draw3DVisual &source);
const Draw3DVisual &ResolveReplacementVisual(const Draw3DVisual &source) noexcept;
std::string_view ReplacementVisualError(const Draw3DVisual &source) noexcept;
}
