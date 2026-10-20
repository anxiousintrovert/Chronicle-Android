#include "atla.hpp"
#include <algorithm>
#include <cmath>
#include "btactstatus.hpp"
#include "dun/gameloop.hpp"
#include "dungeonmap.hpp"
#include "editatra.hpp"
#include "gamemode.hpp"
#include "main.hpp"
#include "monstorunit.hpp"
#include "savedata.hpp"

extern s32 mode, gameTask, NextMapNo, existFlag;
extern CDungeonMap MainDungeonMap, UraDungeonMap;

namespace mods {
namespace {
using Json = nlohmann::json;
void ValidateFloor(const CDungeonMap &map, int dungeon, int floor) {
    if (dungeon < 0 || dungeon > 6 || floor < 0 || floor >= maxFloorTbl__3[dungeon] ||
        map.map_type != 1 || map.atra_num < 0 || map.atra_num > 8)
        throw std::runtime_error("Atla requires a valid native procedural floor and sphere table");
    if (dungeon == 6 && map.atra_num != 0)
        throw std::runtime_error("Demon Shaft has no native Atla registry");
    if (dungeon < 6 && floor >= 40) throw std::runtime_error("Atla floor exceeds native registry bounds");
}

const ATRA_SAVE &Registry(CSaveData &save, const ATRA_BOLL &ball, int dungeon) {
    if (dungeon < 0 || dungeon >= 6 || ball.atra_no < 0 || ball.atra_no >= 100)
        throw std::runtime_error("Atla registry index outside native bounds");
    const auto &entry = save.GetDngStatus()->atra_registry[dungeon][ball.atra_no];
    if (entry.id < 0 || entry.id >= 40 + 128 || entry.refcount <= 0)
        throw std::runtime_error("Atla registry entry is missing or no longer referenced");
    return entry;
}

Json Contents(CSaveData &save, int dungeon, int id) {
    if (id < 40) {
        const auto *part = save.GetEditPartsInfo(dungeon, id);
        if (!part || !GetEditAtraPartsData(dungeon, id)) throw std::runtime_error("Atla part definition unavailable");
        return {{"kind", "part"}, {"id", id}, {"obtained", part->obtained != 0}, {"stock", part->stock}};
    }
    const int chip = id - 40;
    const auto *elements = save.GetElemData(dungeon);
    if (!elements || !GetEditAtraChipData(dungeon, chip)) throw std::runtime_error("Atla chip definition unavailable");
    return {{"kind", "chip"}, {"id", chip}, {"owned_count", std::count(elements, elements + 128, chip)}};
}

void LiveContext() {
    if (!SaveData || mode != GAME_MODE_DUNGEON || UserStatus != static_cast<CUserStatus *>(SaveData->GetDngStatus()) ||
        (NowDngMap != &MainDungeonMap && NowDngMap != &UraDungeonMap) ||
        selectMapNo != SaveData->GetDngStatus()->cur_georama)
        throw std::runtime_error("Atla requires the active native dungeon save and map");
    ValidateFloor(*NowDngMap, selectMapNo, UserStatus->cur_floor);
}
}

std::string ValidateAtla(std::string_view operation, const Json &a) {
    if (!a.is_object()) return "Atla arguments must be an object";
    if (operation == "georama.atla_list") return a.empty() ? "" : "atla_list takes no arguments";
    if (operation != "georama.atla_collect" || a.size() != 1 || !a.contains("slot") ||
        !a["slot"].is_number_integer() || a["slot"].get<double>() < 0 || a["slot"].get<double>() > 7)
        return "provide the current floor's Atla slot in 0..7";
    return {};
}

Json ReadAtlaSnapshot(CSaveData &save, const CDungeonMap &map, int dungeon, int floor, bool back) {
    ValidateFloor(map, dungeon, floor);
    Json spheres = Json::array();
    for (int slot = 0; slot < map.atra_num; ++slot) {
        const auto &ball = map.atra[slot];
        if (!ball.used) continue;
        const auto &entry = Registry(save, ball, dungeon);
        for (int axis = 0; axis < 3; ++axis) if (!std::isfinite(ball.pos[axis])) throw std::runtime_error("Atla position is not finite");
        spheres.push_back({{"slot", slot}, {"registry", ball.atra_no}, {"atla", entry.id},
                           {"position", {ball.pos[0], ball.pos[1], ball.pos[2]}}, {"registry_floor", entry.floor},
                           {"registry_refcount", entry.refcount}, {"contents", Contents(save, dungeon, entry.id)}});
    }
    const auto count = spheres.size();
    return {{"dungeon", dungeon}, {"floor", floor}, {"back", back}, {"count", count}, {"spheres", std::move(spheres)}};
}

bool CollectAtla(CSaveData &save, CDungeonMap &map, int dungeon, int floor, int slot, std::string &error) {
    try {
        ValidateFloor(map, dungeon, floor);
        if (save.GetDngStatus()->cur_chara != 0) throw std::runtime_error("only Toan can collect Atla");
        if (slot < 0 || slot >= map.atra_num || !map.atra[slot].used) throw std::runtime_error("Atla slot is no longer available");
        const auto &ball = map.atra[slot];
        const auto &entry = Registry(save, ball, dungeon);
        Contents(save, dungeon, entry.id);
        const auto &grid = save.GetDngStatus()->atra_grid[dungeon][floor];
        if (std::find(std::begin(grid), std::end(grid), ball.atra_no) == std::end(grid) &&
            std::find(std::begin(grid), std::end(grid), -2) == std::end(grid))
            throw std::runtime_error("Atla registry entry does not belong to the active floor");
        auto staged = std::make_unique<CSaveData>(save);
        const int granted = entry.id < 40 ? staged->AtraPartsGet(dungeon, entry.id) : staged->AtraChipGet(dungeon, entry.id - 40);
        if (!granted) throw std::runtime_error("native Atla grant rejected; sphere and save are unchanged");
        staged->GetDngStatus()->GetAtraData(dungeon, floor, ball.atra_no);
        // All fallible work is complete; these publications cannot allocate.
        save = *staged;
        for (int i = 0; i < map.MapEventLimit(); ++i) {
            if (map.events[i].kind == DNG_EVENT_ATRA && map.events[i].index == slot) map.events[i].kind = DNG_EVENT_NONE;
        }
        map.atra[slot].used = 0;
        error.clear();
        return true;
    } catch (const std::exception &e) { error = e.what(); return false; }
}

Json ReadAtla() {
    LiveContext();
    return ReadAtlaSnapshot(*SaveData, *NowDngMap, selectMapNo, UserStatus->cur_floor, BtUraDongeon != 0);
}

bool ApplyAtla(const std::vector<GameCommand> &commands, int money, std::string &error) {
    try {
        if (commands.size() != 1 || money != 0 || commands[0].operation != "georama.atla_collect" || commands[0].owner.empty())
            throw std::runtime_error("Atla collection must be the owner's only engine write, including currency");
        const auto validation = ValidateAtla(commands[0].operation, commands[0].arguments);
        if (!validation.empty()) throw std::runtime_error(validation);
        LiveContext();
        if (gameTask != GAME_TASK_PLAY || BtEventMode || !BtAllDrawFlag || BtActStatus.action_on || BtActStatus.action_busy ||
            NextMapNo >= 0 || existFlag || BtEventInfo.request != BT_REQUEST_NONE)
            throw std::runtime_error("Atla collection requires stable dungeon play outside actions, menus and transitions");
        return CollectAtla(*SaveData, *NowDngMap, selectMapNo, UserStatus->cur_floor, commands[0].arguments.at("slot"), error);
    } catch (const std::exception &e) { error = e.what(); return false; }
}
}
