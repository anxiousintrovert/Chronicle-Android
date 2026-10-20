#include "world_transition.hpp"

#include <algorithm>
#include <array>

#include "btactstatus.hpp"
#include "character.hpp"
#include "dataread_port.hpp"
#include "dun/gameloop.hpp"
#include "editloop.hpp"
#include "editmapscript.hpp"
#include "gamemode.hpp"
#include "main.hpp"
#include "monstorunit.hpp"
#include "quick_items.hpp"
#include "savedata.hpp"

extern s32 mode, gameTask, NextMapNo;
extern s32 existFlag;
extern int start_event_no, start_system_event, viewMode;

namespace mods {
namespace {
WorldTransitionQueue queue;
constexpr std::array names{"Norune Village", "Matataki Village", "Queens", "Muska Lacka", "Yellow Drops"};
constexpr std::array dungeon_names{"Divine Beast Cave", "Wise Owl Forest", "Shipwreck", "Sun and Moon Temple", "Moon Sea", "Gallery of Time", "Demon Shaft"};
DungeonEntryRequest  floor_request;

WorldTransitionContext Context() {
    WorldTransitionContext c{SaveData, mode, MapNo, SaveData ? int(SaveData->GetDngStatus()->cur_chara) : -1, {}};
    if (!SaveData) {
        c.unavailable = "no active save";
    } else if (NextMapNo >= 0) {
        c.unavailable = "a native map transition is pending";
    } else if (mode == GAME_MODE_EDIT) {
        if (MapNo < 0 || MapNo >= 6 || GameMode != ED_MODE_WALK || EdInteriorFlag || viewMode || start_event_no > 0 || start_system_event > 0) {
            c.unavailable = "requires outdoor town walking outside events, menus and first-person view";
        } else if (!pEditGround || !MainChara.frame) {
            c.unavailable = "native town resources unavailable";
        }
    } else if (mode == GAME_MODE_DUNGEON) {
        if (gameTask != GAME_TASK_PLAY || BtGameModeFlag != BT_GAME_MODE_PLAY || BtEventMode || existFlag ||
            BtActStatus.action_on || BtActStatus.action_busy || BtActStatus.movement_locked || !BtActStatus.can_act) {
            c.unavailable = "requires ordinary dungeon play outside actions and events";
        } else if (UserStatus != static_cast<CUserStatus *>(SaveData->GetDngStatus()) || !NowDngMap || !NowMonstorUnit || !CharaMain.frame || !BtAllDrawFlag) {
            c.unavailable = "native dungeon resources unavailable";
        }
    } else {
        c.unavailable = "requires an active town or dungeon";
    }
    if (c.unavailable.empty()) {
        const auto &s = *SaveData->GetDngStatus();
        if (s.party_size < 1 || s.party_size > 6 || c.character < 0 || c.character >= s.party_size) {
            c.unavailable = "invalid native party state";
        }
    }
    return c;
}

std::string Assets(int map, int) {
    const auto path = map >= 200 ? "dun/script/d0" + std::to_string(map - 199) + "/event.stb" : "gedit/e0" + std::to_string(map + 1) + "/event.stb";
    return DataFileAvailable(path) ? "" : "destination event resource is unavailable: " + path;
}
} // namespace

std::string ValidateWorldTransition(std::string_view op, const nlohmann::json &a) {
    if (!a.is_object()) {
        return "arguments must be an object";
    }
    if (op == "world.locations" || op == "world.transition_state" || op == "dungeon.locations") {
        return a.empty() ? "" : "query takes no arguments";
    }
    if (op == "dungeon.enter") {
        if (!a.contains("dungeon") || !a["dungeon"].is_number_integer() || a["dungeon"].get<double>() < 0 || a["dungeon"].get<double>() > 6) {
            return "provide a retail dungeon integer from 0 to 6";
        }
        for (const auto &[key, value] : a.items()) {
            if (key != "dungeon" && key != "floor") {
                return "unknown dungeon entry argument";
            }
        }
        const int dungeon = a["dungeon"];
        if (a.contains("floor") && (!a["floor"].is_number_integer() || a["floor"].get<double>() < 0 || a["floor"].get<double>() >= maxFloorTbl__3[dungeon])) {
            return "floor must be a zero-based native floor index within this dungeon";
        }
        return {};
    }
    if (op != "world.transition" || a.size() != 1 || !a.contains("map") || !a["map"].is_number_integer() ||
        a["map"].get<double>() < 0 || a["map"].get<double>() >= names.size()) {
        return "provide only map, a retail settlement integer from 0 to 4";
    }
    return {};
}

bool WorldTransitionQueue::Queue(const WorldTransitionContext &c, std::uint64_t epoch,
                                 const std::vector<GameCommand> &commands, int money, const Preflight &preflight, std::string &error) {
    if (commands.size() != 1 || (commands[0].operation != "world.transition" && commands[0].operation != "dungeon.enter") || money) {
        error = "travel must be the callback's only engine write, including currency";
        return false;
    }
    const auto &command = commands[0];
    error = ValidateWorldTransition(command.operation, command.arguments);
    if (!error.empty()) {
        return false;
    }
    if (command.owner.empty()) {
        error = "transition requires a runtime owner";
        return false;
    }
    if (pending_) {
        error = "transition already pending for " + pending_->owner;
        return false;
    }
    if (!c.save || !c.unavailable.empty()) {
        error = c.unavailable.empty() ? "no active save" : c.unavailable;
        return false;
    }
    const bool dungeon = command.operation == "dungeon.enter";
    const int  target = dungeon ? 200 + command.arguments.at("dungeon").get<int>() : command.arguments.at("map").get<int>();
    const int  floor = dungeon ? command.arguments.value("floor", -1) : -1;
    if (c.mode == GAME_MODE_EDIT && c.map == target) {
        state_ = {
            {"state", "unchanged"  },
            {"owner", command.owner},
            {"map",   target       }
        };
        return true;
    }
    if (preflight) {
        error = preflight(target, floor);
    }
    if (!error.empty()) {
        return false;
    }
    state_ = {
        {"state", "queued"     },
        {"owner", command.owner},
        {"map",   target       }
    };
    if (dungeon) {
        state_["floor"] = floor;
    }
    pending_ = Request{c, epoch, command.owner, target, floor};
    return true;
}

void WorldTransitionQueue::Finish(const WorldTransitionContext &c, std::uint64_t epoch, std::span<const std::string> owners,
                                  const Preflight &preflight, const std::function<void(int, int, std::string_view)> &dispatch) {
    if (!pending_) {
        return;
    }
    auto request = std::move(*pending_);
    pending_.reset();
    std::string error;
    bool        cancelled = false;
    if (epoch != request.epoch || std::ranges::find(owners, request.owner) == owners.end()) {
        error = "request owner stopped or runtime restarted";
        cancelled = true;
    } else if (c.save != request.from.save || c.mode != request.from.mode || c.map != request.from.map || c.character != request.from.character) {
        error = "native scene or character changed before dispatch";
    } else {
        error = c.unavailable;
    }
    if (error.empty() && preflight) {
        error = preflight(request.target, request.floor);
    }
    if (error.empty() && !dispatch) {
        error = "native transition dispatcher unavailable";
    }
    if (error.empty()) {
        dispatch(request.target, request.floor, request.owner);
    }
    state_ = {
        {"state", error.empty() ? "dispatched" : cancelled ? "cancelled"
                                                           : "failed"},
        {"owner", request.owner                                                       },
        {"map",   request.target                                                      },
        {"error", error                                                               }
    };
    if (request.target >= 200) {
        state_["floor"] = request.floor;
    }
}

nlohmann::json ReadWorldLocations() {
    auto result = nlohmann::json::array();
    for (int map = 0; map < int(names.size()); ++map) {
        result.push_back({
            {"map",  map       },
            {"name", names[map]}
        });
    }
    return result;
}

nlohmann::json ReadWorldTransition() {
    const auto c = Context();
    return {
        {"ready",   c.unavailable.empty() && !queue.Pending()                      },
        {"reason",  queue.Pending() ? "a mod transition is pending" : c.unavailable},
        {"request", queue.State()                                                  }
    };
}

bool QueueWorldTransition(const std::vector<GameCommand> &commands, int money, std::string &error) {
    return queue.Queue(Context(), Scripts().Epoch(), commands, money, Assets, error);
}

void FinishWorldTransition() {
    if (!queue.Pending()) {
        return;
    }
    const auto owners = Scripts().ActiveOwners();
    queue.Finish(Context(), Scripts().Epoch(), owners, Assets, [](int target, int floor, std::string_view owner) {
        floor_request.Reset();
        if (target >= 200 && floor >= 0) {
            floor_request.Set(SaveData, Scripts().Epoch(), std::string(owner), target - 200, floor);
        }
        CancelQuickUse();
        MapJump(target, -1);
        if (mode == GAME_MODE_EDIT) {
            EdEventInfo.integer_arguments[0] = target;
            start_event_no = -1;
            start_system_event = 2;
        } else {
            existFlag = 2; // Native GameLoop exit performs gate-key/status/audio cleanup.
        }
    });
}

nlohmann::json ReadDungeonLocations() {
    auto result = nlohmann::json::array();
    for (int dungeon = 0; dungeon < 7; ++dungeon) {
        result.push_back({
            {"dungeon",     dungeon                },
            {"name",        dungeon_names[dungeon] },
            {"floor_count", maxFloorTbl__3[dungeon]}
        });
    }
    return result;
}

void DungeonEntryRequest::Set(const void *save, std::uint64_t epoch, std::string owner, int dungeon, int floor) {
    request_ = Request{save, epoch, std::move(owner), dungeon, floor};
}

bool DungeonEntryRequest::Matches(const void *save, std::uint64_t epoch, int dungeon, std::span<const std::string> owners) const {
    return request_ && save && request_->save == save && request_->epoch == epoch && request_->dungeon == dungeon &&
           dungeon >= 0 && dungeon < 7 && request_->floor >= 0 && request_->floor < maxFloorTbl__3[dungeon] &&
           std::ranges::find(owners, request_->owner) != owners.end();
}

void DungeonEntryRequest::Prepare(const void *save, std::uint64_t epoch, int dungeon, std::span<const std::string> owners) {
    if (!request_) {
        return;
    }
    if (request_->prepared || !Matches(save, epoch, dungeon, owners)) {
        Reset();
        return;
    }
    request_->prepared = true;
}

int DungeonEntryRequest::Take(const void *save, std::uint64_t epoch, int dungeon, std::span<const std::string> owners) {
    const int floor = request_ && request_->prepared && Matches(save, epoch, dungeon, owners) ? request_->floor : -1;
    Reset();
    return floor;
}

void PrepareDungeonEntry() {
    if (!floor_request.Pending()) {
        return;
    }
    const auto owners = Scripts().ActiveOwners();
    floor_request.Prepare(SaveData, Scripts().Epoch(), mode == GAME_MODE_DUNGEON ? selectMapNo : -1, owners);
}

int TakeDungeonEntryFloor(int dungeon) {
    if (!floor_request.Pending()) {
        return -1;
    }
    const auto owners = Scripts().ActiveOwners();
    return floor_request.Take(SaveData, Scripts().Epoch(), mode == GAME_MODE_DUNGEON ? dungeon : -1, owners);
}
} // namespace mods
