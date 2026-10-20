#pragma once
#include <cstddef>
#include <span>
#include <vector>

// Internal opaque snapshot of the native generator's scratch arrays.
std::vector<std::byte> CaptureDungeonBuildScratch();
bool                   RestoreDungeonBuildScratch(std::span<const std::byte> bytes) noexcept;
