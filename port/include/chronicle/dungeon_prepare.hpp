#pragma once
#include <cstdint>
#include <memory>

#include "chronicle/dungeon_build.hpp"
#include "chronicle/dungeon_scratch.hpp"
#include "dngstatusdata.hpp"
#include "dungeoneventman.hpp"
#include "dungeonmap.hpp"

namespace chronicle::dungeon {
struct PreparedMaps {
    std::unique_ptr<CDungeonMap>    front, back;
    std::unique_ptr<CDngStatusData> status;
    std::vector<std::byte>          build_scratch;
    std::uint32_t                   random_state;
};

// Synchronous engine-thread preparation only: no frame transforms, live-map publication or
// RNG advancement escapes this call. The caller must separately validate the
// transport script and publish/setup the accepted pair at a native load boundary.
PreparedMaps PrepareMaps(const CDungeonMap &model, const CDungeonEventMan &events,
                         const CDngStatusData &status, BuildLayout layout);
} // namespace chronicle::dungeon
