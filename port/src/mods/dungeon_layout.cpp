#include "dungeon_layout.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <type_traits>

#include "chronicle/dungeon_prepare.hpp"
#include "chronicle/dungeon_transport.hpp"
#include "chronicle/random_state.hpp"
#include "dun/gameloop.hpp"
#include "randomitem.hpp"
#include "savedata.hpp"

extern CDungeonMap UraDungeonMap;
extern CRandomItem MainRandomItem, SubRandomItem;
extern CTexture   *GoldTex;

namespace mods {
namespace {
using Json = nlohmann::json;

struct Claim {
    std::string                     owner;
    chronicle::dungeon::BuildLayout layout;
    std::uint64_t                   epoch;
    Json                            result;
};

std::map<int, Claim> claims;

void ResetEvents(CDungeonEventMan &manager) {
    for (auto &event : manager.event) {
        event.event = nullptr;
        event.switch_on = event.enabled = false;
        event.hold = 0;
        event.chara_done = -1;
    }
    manager.port_event_limit = chronicle::dungeon::RetailRuntimeEventCapacity;
}
} // namespace

bool HasOwnedLayouts() { return !claims.empty(); }

bool IsLayoutWrite(std::string_view op) { return op == "dungeon.layout_set" || op == "dungeon.layout_reset"; }

std::string ValidateLayout(std::string_view op, const Json &a) {
    if (op != "dungeon.layout_get" && !IsLayoutWrite(op)) {
        return "unknown layout operation";
    }
    if (!a.is_object() || !a.contains("dungeon") || !a["dungeon"].is_number_integer() ||
        a["dungeon"].get<double>() < 0 || a["dungeon"].get<double>() > 6) {
        return "dungeon must be in 0..6";
    }
    if (op != "dungeon.layout_set") {
        return a.size() == 1 ? "" : "layout get/reset takes only dungeon";
    }
    if (a.size() != 5 + a.contains("chests") + a.contains("monsters")) {
        return "layout_set requires dungeon, rooms, area, room_min and room_max; chests and monsters are optional";
    }
    if (a.contains("chests") && (!a["chests"].is_number_integer() || a["chests"].get<double>() < 2 || a["chests"].get<double>() > 24)) {
        return "chests must be in 2..24";
    }
    if (a.contains("monsters") && (!a["monsters"].is_number_integer() || a["monsters"].get<double>() < 2 || a["monsters"].get<double>() > 15)) {
        return "monsters must be in 2..15";
    }
    if (a.contains("monsters") && a["dungeon"] == 1 && a["monsters"].get<int>() < 3) {
        return "Wise Owl Forest requires at least three key carriers";
    }
    for (const auto &[field, bounds] : std::map<std::string, std::pair<int, int>>{
             {"rooms",    {2, 64}},
             {"area",     {8, 46}},
             {"room_min", {3, 6} },
             {"room_max", {3, 6} }
    }) {
        if (!a.contains(field) || !a[field].is_number_integer() || a[field].get<double>() < bounds.first || a[field].get<double>() > bounds.second) {
            return "invalid layout field: " + field;
        }
    }
    return a["room_min"].get<int>() <= a["room_max"].get<int>() ? "" : "room_min must not exceed room_max";
}

Json ReadLayout(const Json &a) {
    const auto error = ValidateLayout("dungeon.layout_get", a);
    if (!error.empty()) {
        throw std::runtime_error(error);
    }
    const int dungeon = a.at("dungeon");
    Json      out{
        {"dungeon",    dungeon             },
        {"scope",      "future_floor_loads"},
        {"enabled",    false               },
        {"owner",      ""                  },
        {"requested",  nullptr             },
        {"last_build", nullptr             }
    };
    auto found = claims.find(dungeon);
    if (found != claims.end()) {
        const auto &claim = found->second;
        out["enabled"] = true;
        out["owner"] = claim.owner;
        out["requested"] = {
            {"rooms",    claim.layout.rooms   },
            {"area",     claim.layout.area    },
            {"room_min", claim.layout.room_min},
            {"room_max", claim.layout.room_max}
        };
        out["last_build"] = claim.result;
        if (claim.layout.chests != 0) out["requested"]["chests"] = claim.layout.chests;
        if (claim.layout.monsters != 0) out["requested"]["monsters"] = claim.layout.monsters;
    }
    return out;
}

void RetainOwnedLayouts(std::span<const std::string> owners) {
    std::erase_if(claims, [&](const auto &entry) { return entry.second.epoch != Scripts().Epoch() || std::ranges::find(owners, entry.second.owner) == owners.end(); });
}

struct LayoutTransaction::Impl {
    std::map<int, Claim> values = claims;
};

LayoutTransaction::LayoutTransaction() = default;
LayoutTransaction::~LayoutTransaction() = default;

bool LayoutTransaction::Stage(const std::vector<GameCommand> &commands, std::string &error) {
    staged_.reset();
    try {
        if (commands.size() > 64) {
            throw std::runtime_error("callback write budget exceeded");
        }
        for (const auto &command : commands) {
            if (!IsLayoutWrite(command.operation)) {
                continue;
            }
            error = ValidateLayout(command.operation, command.arguments);
            if (!error.empty()) {
                throw std::runtime_error(error);
            }
            if (command.owner.empty()) {
                throw std::runtime_error("layout writes require a package owner");
            }
            if (!staged_) {
                staged_ = std::make_unique<Impl>();
            }
            const int dungeon = command.arguments.at("dungeon");
            auto      found = staged_->values.find(dungeon);
            if (found != staged_->values.end() && found->second.owner != command.owner) {
                throw std::runtime_error("dungeon " + std::to_string(dungeon) + " layout is already owned by " + found->second.owner);
            }
            if (command.operation == "dungeon.layout_reset") {
                staged_->values.erase(dungeon);
                continue;
            }
            const auto &a = command.arguments;
            staged_->values[dungeon] = {
                command.owner, {a.at("rooms"), a.at("area"), a.at("room_min"), a.at("room_max"), a.value("chests", 0), a.value("monsters", 0)},
                 Scripts().Epoch(), {{"state", "pending"}}
            };
        }
        error.clear();
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        staged_.reset();
        return false;
    }
}

void LayoutTransaction::Commit() noexcept {
    if (staged_) {
        claims.swap(staged_->values);
    }
    staged_.reset();
}

bool BuildOwnedDungeonLayout() {
    if (claims.empty()) {
        return false;
    }
    RetainOwnedLayouts(Scripts().ActiveOwners());
    auto found = claims.find(selectMapNo);
    if (found == claims.end()) {
        return false;
    }
    auto &claim = found->second;
    // All fallible preparation precedes publication; failures take the unchanged
    // native path with the original RNG, map pointers, save and scratch state.
    std::optional<chronicle::dungeon::PreparedMaps> prepared;
    try {
        PrepareLoadedExpandedTransport(selectMapNo);
        if (!SaveData || UserStatus != static_cast<CUserStatus *>(SaveData->GetDngStatus())) {
            throw std::runtime_error("native dungeon save is unavailable");
        }
        prepared.emplace(chronicle::dungeon::PrepareMaps(MainDungeonMap, DngEventMan, *SaveData->GetDngStatus(), claim.layout));
        claim.result = {
            {"state",       "applied"                          },
            {"floor",       SaveData->GetDngStatus()->cur_floor},
            {"front_rooms", prepared->front->room_num          },
            {"back_rooms",  prepared->back->room_num           },
            {"front_chests", prepared->front->box_num},
            {"back_chests", prepared->back->box_num}
        };
    } catch (const std::exception &e) {
        claim.result = {
            {"state",  "rejected"},
            {"reason", e.what()  }
        };
        return false;
    }
    // Native map assignment copies fields without allocation; its legacy
    // water/NPC assignment signatures are not annotated noexcept.
    static_assert(std::is_nothrow_copy_assignable_v<CDngStatusData>);
    MainDungeonMap = *prepared->front;
    UraDungeonMap = *prepared->back;
    *SaveData->GetDngStatus() = *prepared->status;
    RestoreDungeonBuildScratch(prepared->build_scratch);
    chronicle::RestoreRandomState(prepared->random_state);
    for (auto *items : {&MainRandomItem, &SubRandomItem}) {
        for (int i = 0; i < 32; ++i) {
            items->id[i] = -1;
            items->pickup_event[i] = -1;
            items->pickup_blocked[i] = 0;
        }
        items->gold_texture = GoldTex;
    }
    RandomItem = &MainRandomItem;
    ResetEvents(DngEventMan);
    NowDngMap = &MainDungeonMap;
    NowEventMan = &DngEventMan;
    MainDungeonMap.DrawMapCalc(MainDungeonMap.map_type);
    DngEventMan.SetupEvent(&MainDungeonMap, MainDungeonMap.map_type);
    UraEventMan = DngEventMan;
    ResetEvents(UraEventMan);
    NowDngMap = &UraDungeonMap;
    NowEventMan = &UraEventMan;
    UraDungeonMap.DrawMapCalc(UraDungeonMap.map_type);
    UraEventMan.SetupEvent(&UraDungeonMap, UraDungeonMap.map_type);
    NowDngMap = &MainDungeonMap;
    NowEventMan = &DngEventMan;
    BtUraDongeon = 0;
    BtEquipMap = BtEquipMasuisyou = 0;
    return true;
}
} // namespace mods
