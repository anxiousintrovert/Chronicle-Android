#include "back_floor.hpp"

#include <algorithm>

#include "btactstatus.hpp"
#include "dun/gameloop.hpp"
#include "dungeoneventman.hpp"
#include "dungeonmap.hpp"
#include "gameloop.hpp"
#include "main.hpp"
#include "monstorunit.hpp"
#include "savedata.hpp"

extern s32         mode, gameTask, NextMapNo, existFlag;
extern CDungeonMap MainDungeonMap, UraDungeonMap;

namespace mods {
namespace {
BackFloorQueue queue;

struct ActiveTransport {
    CSaveData *save;
    int        dungeon, floor, seed;
};

std::optional<ActiveTransport> transport;

const CDungeonEventData *Transport() {
    for (int i = 0; i < DngEventMan.EventLimit(); ++i) {
        const auto &event = DngEventMan.event[i];
        if (event.enabled && event.event && event.event->parts_id == MAP_PARTS_URA_ENTRANCE_NORTH && event.event->script_no == (selectMapNo == 4 ? 201 : 200)) {
            return &event;
        }
    }
    return nullptr;
}

BackFloorContext Context(bool boundary) {
    BackFloorContext c;
    c.save = SaveData;
    c.map = NowDngMap;
    c.dungeon = selectMapNo;
    c.floor = SaveData ? SaveData->GetDngStatus()->cur_floor : -1;
    c.active = BtUraDongeon != 0;
    c.boundary = boundary && gameTask == GAME_TASK_PLAY && !BtEventMode && !BtActStatus.action_on && !BtActStatus.action_busy && Transport();
    c.native_request = BtEventInfo.request != BT_REQUEST_NONE;
    if (!SaveData || mode != GAME_MODE_DUNGEON || UserStatus != static_cast<CUserStatus *>(SaveData->GetDngStatus())) {
        c.unavailable = "requires an active native dungeon save";
    } else if (c.dungeon < 0 || c.dungeon >= 7 || c.floor < 0 || c.floor >= maxFloorTbl__3[c.dungeon] || !BtUraEnemyLayoutList[c.dungeon]) {
        c.unavailable = "no native back-floor layout for this dungeon/floor";
    } else if ((NowDngMap != &MainDungeonMap && NowDngMap != &UraDungeonMap) || MainDungeonMap.map_type != 1 || UraDungeonMap.map_type != 1 || !NowMonstorUnit || !BtAllDrawFlag) {
        c.unavailable = "requires loaded native procedural front and back floors";
    } else if (NextMapNo >= 0 || existFlag) {
        c.unavailable = "a native scene transition is pending";
    } else if (gameTask != GAME_TASK_PLAY && gameTask != GAME_TASK_SCRIPT_RUN && gameTask != GAME_TASK_SCRIPT_START && gameTask != GAME_TASK_SCRIPT_RESTART) {
        c.unavailable = "requires dungeon play or an active native script";
    }
    if (c.unavailable.empty() && !c.active && !Transport()) {
        c.unavailable = "native transport event unavailable on this floor";
    }
    return c;
}
} // namespace

nlohmann::json ReadBackFloor() {
    const auto  c = Context(false);
    std::string reason = c.unavailable;
    if (reason.empty() && queue.Pending()) {
        reason = "a back-floor request is pending";
    }
    if (reason.empty() && transport) {
        reason = "native transport is running";
    }
    if (reason.empty() && gameTask != GAME_TASK_PLAY) {
        reason = "requires ordinary dungeon play";
    }
    nlohmann::json result = {
        {"ready",   c.unavailable.empty() && !queue.Pending() && !transport && gameTask == GAME_TASK_PLAY},
        {"reason",  reason                                                                               },
        {"active",  c.active                                                                             },
        {"dungeon", c.dungeon                                                                            },
        {"floor",   c.floor                                                                              },
        {"pending", queue.Pending()                                                                      },
        {"request", queue.State()                                                                        }
    };
    if (c.unavailable.empty()) {
        const int key = NowDngMap->gate_key_no;
        result["floor_gate_key"] = {
            {"item",  key                                                      },
            {"count", key > 0 ? SaveData->GetDngStatus()->CheckItemGet(key) : 0}
        };
    }
    return result;
}

bool QueueBackFloor(const std::vector<GameCommand> &commands, int money, std::string &error) {
    if (gameTask != GAME_TASK_PLAY || transport) {
        error = "requires ordinary dungeon play with no transport running";
        return false;
    }
    return queue.Queue(Context(false), Scripts().Epoch(), GameFrameCount(), commands, money, error);
}

void PollBackFloor() {
    if (transport) {
        if (SaveData != transport->save || mode != GAME_MODE_DUNGEON || selectMapNo != transport->dungeon || !SaveData || SaveData->GetDngStatus()->cur_floor != transport->floor || MainDungeonMap.map_seed != transport->seed) {
            transport.reset();
        } else if (gameTask == GAME_TASK_PLAY && !BtEventMode) {
            transport.reset();
        }
    }
    if (!queue.Pending()) {
        return;
    }
    const auto owners = Scripts().ActiveOwners();
    if (queue.Step(Context(true), Scripts().Epoch(), GameFrameCount(), owners)) {
        const auto *event = Transport();
        if (!event) {
            return;
        }
        auto *status = SaveData->GetDngStatus();
        transport = ActiveTransport{SaveData, selectMapNo, status->cur_floor, MainDungeonMap.map_seed};
        BtEventInfo.script_no = event->event->script_no;
        BtEventInfo.ext_memory = event->event->ext_mem;
        BtEventInfo.event_marker = true;
        BtEventInfo.action_mode = 1;
        for (int i = 0; i < 4; ++i) {
            BtEventInfo.position[i] = event->pos[i];
            BtEventInfo.direction[i] = event->dir[i];
        }
        gameTask = GAME_TASK_SCRIPT_START;
    }
}

bool BackFloorTransportAccess(int flag) {
    return transport && SaveData == transport->save && mode == GAME_MODE_DUNGEON &&
           selectMapNo == transport->dungeon && SaveData &&
           SaveData->GetDngStatus()->cur_floor == transport->floor && MainDungeonMap.map_seed == transport->seed &&
           BtEventInfo.running_script_no == (selectMapNo == 4 ? 201 : 200) && flag == (selectMapNo == 0 ? 1 : 0);
}

bool BackFloorQueue::Queue(const BackFloorContext &c, std::uint64_t epoch, std::uint64_t frame,
                           const std::vector<GameCommand> &commands, int money, std::string &error) {
    error.clear();
    if (commands.size() != 1 || commands[0].operation != "dungeon.back_floor_enter" || money) {
        error = "back-floor entry must be the callback's only engine write, including currency";
        return false;
    }
    const auto &command = commands[0];
    if (!command.arguments.is_object() || !command.arguments.empty()) {
        error = "back-floor entry takes no arguments";
    } else if (command.owner.empty()) {
        error = "back-floor entry requires a runtime owner";
    } else if (pending_) {
        error = "back-floor entry already pending for " + pending_->owner;
    } else if (!c.save || !c.map || c.dungeon < 0 || c.dungeon >= 7 || c.floor < 0) {
        error = "no valid native floor context";
    } else if (!c.unavailable.empty()) {
        error = c.unavailable;
    }
    if (!error.empty()) {
        return false;
    }
    state_ = {
        {"state",   c.active ? "unchanged" : "queued"},
        {"owner",   command.owner                    },
        {"dungeon", c.dungeon                        },
        {"floor",   c.floor                          }
    };
    if (!c.active) {
        pending_ = Request{c, epoch, frame, command.owner};
    }
    return true;
}

void BackFloorQueue::Complete(std::string_view state, std::string_view reason) {
    state_["state"] = state;
    state_["reason"] = reason;
    pending_.reset();
}

bool BackFloorQueue::Step(const BackFloorContext &c, std::uint64_t epoch, std::uint64_t frame,
                          std::span<const std::string> owners) {
    if (!pending_) {
        return false;
    }
    const auto &r = *pending_;
    if (epoch != r.epoch || std::ranges::find(owners, r.owner) == owners.end()) {
        Complete("cancelled", "request owner stopped or runtime restarted");
    } else if (c.save != r.from.save || c.dungeon != r.from.dungeon || c.floor != r.from.floor) {
        Complete("cancelled", "native save or floor changed");
    } else if (c.active) {
        Complete("satisfied", "native back floor already active");
    } else if (c.map != r.from.map) {
        Complete("cancelled", "native map changed");
    } else if (!c.unavailable.empty()) {
        Complete("cancelled", c.unavailable);
    } else if (frame < r.frame || frame - r.frame >= lifetime_frames) {
        Complete("expired", "no native script boundary before request deadline");
    } else if (c.boundary && !c.native_request) {
        Complete("dispatched", "native back-floor transport started");
        return true;
    }
    return false;
}
} // namespace mods
