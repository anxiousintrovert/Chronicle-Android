#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>
namespace mods {
// Detached author geometry, not an engine allocation or a custom-content ID.
struct MeshAsset {
    struct Triangle { std::uint32_t strip; std::array<std::uint32_t,3> vertices; };
    std::vector<std::array<float,8>> vertices;
    std::vector<Triangle> triangles;
};
// Throws on malformed, ambiguous, oversized or float32-degenerate geometry.
// No filesystem, native state, graphics allocation or RNG access.
MeshAsset DecodeMeshAsset(std::string_view source);
}
