#pragma once
#include <nlohmann/json.hpp>

#include <array>
#include <string>
#include <string_view>

class CSaveData;

namespace mods {
// Detached view of the loaded town's optional elements and request cache.
// Not a definition registry or an ownership model for custom content.
struct GeoramaState {
    struct Element {
        int  id = -1, required = -1;
        bool enabled = false;
    };

    struct Plot {
        bool                   available = false;
        std::array<Element, 6> elements;
    };

    int                  town = -1;
    int                  request_count = -1;
    std::array<int, 24> requests{};
    bool                 requests_changed = false;
    std::array<Plot, 24> plots;
};

std::string    ValidateGeoramaRequest(const nlohmann::json &arguments);
void           ApplyGeoramaRequest(CSaveData &save, GeoramaState *scene, const nlohmann::json &arguments);
std::string    ValidateGeoramaElements(std::string_view operation, const nlohmann::json &arguments);
nlohmann::json ReadGeoramaElements(CSaveData &save, const GeoramaState &scene, const nlohmann::json &arguments);
void           ApplyGeoramaElement(CSaveData &save, GeoramaState &scene, const nlohmann::json &arguments);
} // namespace mods
