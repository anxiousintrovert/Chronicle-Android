#include "georama_placement.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include "editarea.hpp"
#include "editground.hpp"
#include "editloop.hpp"
#include "editmapscript.hpp"
#include "editpartsinfo.hpp"
#include "gamemode.hpp"
#include "main.hpp"
#include "savedata.hpp"
extern s32 mode;
extern void EditPartsObjectOnOff();

namespace mods {
namespace {
using Json = nlohmann::json;
bool Integer(const Json &a, const char *key, int high) {
    return a.contains(key) && a[key].is_number_integer() && a[key].get<double>() >= 0 && a[key].get<double>() <= high;
}
CEditGround &Ground() {
    if (!SaveData || mode != GAME_MODE_EDIT || GameMode != ED_MODE_WALK || NowEditMap < 0 || NowEditMap > 4 ||
        MapNo != NowEditMap || !pEditGround || pEditGround->map_no != NowEditMap ||
        pEditGround->parts_info != &EditPartsInfo || !pEditGround->plot_parts)
        throw std::runtime_error("placement requires the loaded settlement's stable walking scene");
    if (pEditGround->CheckEffect()) throw std::runtime_error("wait for the native build effect to finish");
    return *pEditGround;
}
void RequestCapacity(const CEditGround &ground, int added_part, int removed_slot) {
    std::array<unsigned, 24> counts{};
    for (int i = 0; i < 128; ++i) {
        const auto &part = ground.parts[i];
        if (i != removed_slot && part.handle >= 0 && part.parts_no >= 0 && part.parts_no < 24) ++counts[part.parts_no];
    }
    if (added_part >= 0 && added_part < 24) ++counts[added_part];
    if (std::ranges::any_of(counts, [](unsigned count) { return count > 64; }))
        throw std::runtime_error("placement exceeds native per-plot request evaluation capacity");
}
}
std::string ValidateGeoramaPlacement(std::string_view op, const Json &a) {
    if (!a.is_object()) return "placement arguments must be an object";
    if (op == "georama.placement_state") return a.empty() ? "" : "placement_state takes no arguments";
    if (!Integer(a, "town", 4)) return "town must identify a settlement in 0..4";
    if (op == "georama.remove") return a.size() == 2 && Integer(a, "slot", 127) ? "" : "provide town and current native placement slot 0..127";
    if (op != "georama.place" || a.size() != 4 || !Integer(a, "part", 23) || !Integer(a, "rotation", 3) ||
        !a.contains("position") || !a["position"].is_array() || a["position"].size() != 3)
        return "provide town, retail part 0..23, rotation 0..3 and position [x,y,z]";
    for (const auto &coordinate : a["position"]) {
        if (!coordinate.is_number() || !std::isfinite(coordinate.get<double>()) || std::abs(coordinate.get<double>()) > 100000)
            return "placement position must be finite and within +/-100000";
    }
    return {};
}
Json ReadGeoramaPlacement() {
    auto &ground = Ground();
    Json parts = Json::array(), placements = Json::array();
    for (int id = 0; id < 24; ++id) {
        const auto *info = EditPartsInfo.GetPartsInfo(id);
        const auto &source = ground.plot_parts[id];
        if (!info || info->parts_no != id || source.handle < 0 || !source.info) continue;
        parts.push_back({{"part", id}, {"stock", info->stock}, {"placed", info->placed},
                         {"available", std::max(0, int(info->stock) - int(info->placed))}});
    }
    for (int slot = 0; slot < 128; ++slot) {
        const auto &part = ground.parts[slot];
        if (part.handle < 0) continue;
        placements.push_back({{"slot", slot}, {"part", part.parts_no},
                              {"rotation", part.rot_y < 0 ? 3 : part.rot_y},
                              {"position", {part.pos[0], part.pos[1], part.pos[2]}}});
    }
    return {{"town", NowEditMap}, {"parts", std::move(parts)}, {"placements", std::move(placements)}};
}
bool ApplyGeoramaPlacement(const std::vector<GameCommand> &commands, int money, std::string &error) {
    try {
        if (commands.size() != 1 || money || commands[0].owner.empty())
            throw std::runtime_error("placement must be the owner's only engine write, including currency");
        const auto &op = commands[0].operation;
        const auto &a = commands[0].arguments;
        const auto validation = ValidateGeoramaPlacement(op, a);
        if (!validation.empty()) throw std::runtime_error(validation);
        auto &ground = Ground();
        if (a.at("town") != NowEditMap) throw std::runtime_error("placement town is not the loaded settlement");
        int result;
        if (op == "georama.place") {
            const int id = a.at("part");
            auto *info = EditPartsInfo.GetPartsInfo(id);
            const auto &source = ground.plot_parts[id];
            if (!info || info->parts_no != id || source.handle < 0 || !source.info || info->placed < 0 || info->stock <= info->placed)
                throw std::runtime_error("retail part is unavailable or has no built stock");
            // Native SetMapParts increments placed before checking this capacity.
            if (std::ranges::none_of(ground.parts, [](const CMapParts &part) { return part.handle < 0; }))
                throw std::runtime_error("native placement table is full");
            if ((source.subtype == MAP_PARTS_SUBTYPE_RIVER || source.subtype == MAP_PARTS_SUBTYPE_BRIDGE || source.subtype == MAP_PARTS_SUBTYPE_ON_RIVER) && !ground.river_parts)
                throw std::runtime_error("native river templates are unavailable");
            if (source.subtype == MAP_PARTS_SUBTYPE_ROAD && !ground.road_parts) throw std::runtime_error("native road templates are unavailable");
            RequestCapacity(ground, source.parts_no, -1);
            if (source.subtype == MAP_PARTS_SUBTYPE_BRIDGE) RequestCapacity(ground, ground.river_parts[6].parts_no, -1);
            const auto &p = a.at("position");
            const int rotation = a.at("rotation");
            // CheckParts rotates its input even when fit is rejected. Keep
            // template transforms unchanged; the placed instance gets a copy.
            struct RestoreTemplate {
                CMapParts &target;
                CMapParts original;
                ~RestoreTemplate() { target = original; }
            } restore{ground.plot_parts[id], ground.plot_parts[id]};
            result = ground.SetMapParts(id, p[0].get<float>(), p[1].get<float>(), p[2].get<float>(), rotation == 3 ? -1 : rotation);
        } else {
            const int slot = a.at("slot");
            const auto &part = ground.parts[slot];
            if (part.handle < 0 || !part.info) throw std::runtime_error("placement slot is no longer occupied");
            for (int axis = 0; axis < 3; ++axis) if (!std::isfinite(part.pos[axis])) throw std::runtime_error("native placement position is invalid");
            if (ground.GetPartsID(part.pos[0], part.pos[1], part.pos[2]) != slot)
                throw std::runtime_error("native removal cursor does not identify the requested slot");
            const bool crossing = part.subtype == MAP_PARTS_SUBTYPE_BRIDGE || part.subtype == MAP_PARTS_SUBTYPE_ON_RIVER;
            if ((crossing || part.subtype == MAP_PARTS_SUBTYPE_RIVER) && !ground.river_parts) throw std::runtime_error("native river templates are unavailable");
            if (part.subtype == MAP_PARTS_SUBTYPE_ROAD && !ground.road_parts) throw std::runtime_error("native road templates are unavailable");
            RequestCapacity(ground, crossing ? ground.river_parts[1].parts_no : -1, slot);
            int removed, rotation;
            result = ground.DeleteMapParts(&removed, &rotation, part.pos[0], part.pos[1], part.pos[2]);
        }
        if (result < 0) throw std::runtime_error("native placement fit or removal dependency checks rejected the request");
        // Same native visibility, collision and request refresh used by editing.
        ground.MakePartsBox();
        EditPartsObjectOnOff();
        ground.Save(NowEditMap, SaveData);
        EditPartsInfo.Save(NowEditMap, SaveData);
        error.clear();
        return true;
    } catch (const std::exception &e) { error = e.what(); return false; }
}
}
