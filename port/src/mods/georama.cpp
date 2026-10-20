#include "georama.hpp"

#include <stdexcept>

#include "savedata.hpp"

namespace mods {
namespace {
const GeoramaState::Plot &Plot(const GeoramaState &scene, const nlohmann::json &a) {
    if (scene.town < 0 || scene.town != a.at("town").get<int>()) {
        throw std::runtime_error("Georama elements require the selected town's loaded editor scene");
    }
    const auto &plot = scene.plots.at(a.at("plot").get<int>());
    if (!plot.available) {
        throw std::runtime_error("native Georama plot definition is unavailable");
    }
    return plot;
}

bool Optional(const GeoramaState::Element &e) { return e.id >= 0 && e.id < 36; }
} // namespace

std::string ValidateGeoramaRequest(const nlohmann::json &a) {
    if (!a.is_object() || a.size() != 3 || !a.contains("town") || !a.contains("request") ||
        !a.contains("completed") || !a["completed"].is_boolean()) return "provide town, request and boolean completed";
    for (const auto *key : {"town", "request"}) {
        if (!a[key].is_number_integer() || a[key].get<double>() < 0 || a[key].get<double>() > (std::string_view(key) == "town" ? 5 : 23))
            return "town must be 0..5 and request must be 0..23";
    }
    return {};
}

void ApplyGeoramaRequest(CSaveData &save, GeoramaState *scene, const nlohmann::json &a) {
    const auto error = ValidateGeoramaRequest(a);
    if (!error.empty()) throw std::runtime_error(error);
    const int town = a.at("town"), request = a.at("request");
    auto &saved = *save.GetGrdData(town);
    if (saved.request_count < 0 || saved.request_count > 24 || request >= saved.request_count)
        throw std::runtime_error("request is not defined by the saved town's valid request table");
    const int value = a.at("completed").get<bool>() ? 1 : 0;
    if (scene && scene->town == town) {
        if (scene->request_count != saved.request_count)
            throw std::runtime_error("loaded and saved Georama request counts do not match");
        scene->requests[request] = value;
        scene->requests_changed = true;
    }
    saved.request_complete[request] = value;
}

std::string ValidateGeoramaElements(std::string_view operation, const nlohmann::json &a) {
    const bool write = operation == "georama.element_set";
    if (!a.is_object() || a.size() != (write ? 4u : 2u) || !a.contains("town") || !a.contains("plot") ||
        (write && (!a.contains("element") || !a.contains("enabled")))) {
        return write ? "provide town, plot, element and enabled" : "provide town and plot";
    }
    for (const auto &[key, value] : a.items()) {
        if (key == "enabled") {
            if (!value.is_boolean()) {
                return "enabled must be a boolean";
            }
        } else if (!value.is_number_integer() || value.get<double>() < 0 || value.get<double>() > (key == "plot" ? 23 : 5)) {
            return "invalid Georama index: " + key;
        }
    }
    return {};
}

nlohmann::json ReadGeoramaElements(CSaveData &save, const GeoramaState &scene, const nlohmann::json &a) {
    const auto &plot = Plot(scene, a);
    const auto *saved = save.GetEditPartsInfo(scene.town, a.at("plot"));
    auto        elements = nlohmann::json::array();
    for (int i = 0; i < 6; ++i) {
        const auto &e = plot.elements[i];
        elements.push_back({
            {"element",          i                             },
            {"native_id",        e.id                          },
            {"optional",         Optional(e)                   },
            {"required_element", e.required                    },
            {"enabled",          e.enabled                     },
            {"saved_enabled",    saved->element_enabled[i] != 0}
        });
    }
    return {
        {"town",     scene.town  },
        {"plot",     a.at("plot")},
        {"elements", elements    }
    };
}

void ApplyGeoramaElement(CSaveData &save, GeoramaState &scene, const nlohmann::json &a) {
    const auto &plot = Plot(scene, a);
    const int   index = a.at("element");
    const auto &element = plot.elements.at(index);
    if (!Optional(element)) {
        throw std::runtime_error("slot is not a native optional plot element");
    }
    const bool enabled = a.at("enabled");
    if (enabled && element.required >= 0) {
        if (element.required >= 6 || !Optional(plot.elements[element.required]) || !plot.elements[element.required].enabled) {
            throw std::runtime_error("enable the required native plot element first");
        }
    }
    if (!enabled) {
        for (const auto &dependent : plot.elements) {
            if (dependent.enabled && dependent.required == index) {
                throw std::runtime_error("another enabled plot element depends on this element");
            }
        }
    }
    save.GetEditPartsInfo(scene.town, a.at("plot"))->element_enabled[index] = enabled ? 1 : 0;
    scene.plots[a.at("plot").get<int>()].elements[index].enabled = enabled;
}
} // namespace mods
