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
#include "quick_items.hpp"
#include "savedata.hpp"

extern s32 mode, gameTask, NextMapNo;
extern s32 existFlag;
extern int start_event_no, start_system_event, viewMode;

namespace mods {
namespace {
WorldTransitionQueue queue;
constexpr std::array names{"Norune Village", "Matataki Village", "Queens", "Muska Lacka", "Yellow Drops"};

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

std::string Assets(int map) {
    const auto path = "gedit/e0" + std::to_string(map + 1) + "/event.stb";
    return DataFileAvailable(path) ? "" : "destination event resource is unavailable: " + path;
}
} // namespace

std::string ValidateWorldTransition(std::string_view op, const nlohmann::json &a) {
    if (!a.is_object()) {
        return "arguments must be an object";
    }
    if (op == "world.locations" || op == "world.transition_state") {
        return a.empty() ? "" : "query takes no arguments";
    }
    if (op != "world.transition" || a.size() != 1 || !a.contains("map") || !a["map"].is_number_integer() ||
        a["map"].get<double>() < 0 || a["map"].get<double>() >= names.size()) {
        return "provide only map, a retail settlement integer from 0 to 4";
    }
    return {};
}

bool WorldTransitionQueue::Queue(const WorldTransitionContext &c, std::uint64_t epoch,
                                 const std::vector<GameCommand> &commands, int money, const Preflight &preflight, std::string &error) {
    if (commands.size() != 1 || commands[0].operation != "world.transition" || money) {
        error = "world.transition must be the callback's only engine write, including currency";
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
    const int target = command.arguments.at("map");
    if (c.mode == GAME_MODE_EDIT && c.map == target) {
        state_ = {
            {"state", "unchanged"  },
            {"owner", command.owner},
            {"map",   target       }
        };
        return true;
    }
    if (preflight) {
        error = preflight(target);
    }
    if (!error.empty()) {
        return false;
    }
    state_ = {
        {"state", "queued"     },
        {"owner", command.owner},
        {"map",   target       }
    };
    pending_ = Request{c, epoch, command.owner, target};
    return true;
}

void WorldTransitionQueue::Finish(const WorldTransitionContext &c, std::uint64_t epoch, std::span<const std::string> owners,
                                  const Preflight &preflight, const std::function<void(int)> &dispatch) {
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
        error = preflight(request.target);
    }
    if (error.empty() && !dispatch) {
        error = "native transition dispatcher unavailable";
    }
    if (error.empty()) {
        dispatch(request.target);
    }
    state_ = {
        {"state", error.empty() ? "dispatched" : cancelled ? "cancelled"
                                                           : "failed"},
        {"owner", request.owner                                                       },
        {"map",   request.target                                                      },
        {"error", error                                                               }
    };
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
    queue.Finish(Context(), Scripts().Epoch(), owners, Assets, [](int target) {
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
} // namespace mods
