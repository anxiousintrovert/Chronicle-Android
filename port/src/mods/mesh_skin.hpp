#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>
namespace mods {
class MeshSkinBinding {
public:
    using Point = std::array<float,3>;
    using Vertex = std::array<float,4>;
    // Binds in native model coordinates; equal-distance ties use the lowest
    // source index. Preparation allocates, pose updates do not.
    static MeshSkinBinding Prepare(std::span<const Point> source, std::span<const Point> replacement, std::span<const std::uint32_t> mapping = {});
    void BeginPose() noexcept;
    // Exact native skin arithmetic; false rejects invalid input without changes.
    bool ApplyBone(std::uint32_t source_vertex, const float base[4][4], const float bone[4][4],
                   const float inverse[4][4], float weight) noexcept;
    std::span<const Vertex> Positions() const noexcept { return positions_; }
    std::span<const std::uint32_t> Sources() const noexcept { return sources_; }
private:
    std::vector<Vertex> rest_, deformed_, positions_;
    std::vector<std::uint32_t> sources_, offsets_, targets_;
};
}
