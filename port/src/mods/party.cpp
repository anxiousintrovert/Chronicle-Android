#include "party.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include "battlemenu.hpp"
#include "bt_shot_effect.hpp"
#include "btactstatus.hpp"
#include "btitem.hpp"
#include "btmisc.hpp"
#include "character.hpp"
#include "dataread.hpp"
#include "dataread_port.hpp"
#include "dun/gameloop.hpp"
#include "main.hpp"
#include "menu_draw.hpp"
#include "savedata.hpp"

extern s32             mode, gameTask;
extern CFrameVu1      *CharaFrame;
extern BT_SHOT_EFFECT *MyEffectEntry_Tbl[15];

namespace mods {
using Json = nlohmann::json;

std::string ValidatePartySwitch(std::string_view op, const Json &a) {
    if (!a.is_object()) {
        return "arguments must be an object";
    }
    if (op == "party.switch_state") {
        return a.empty() ? "" : "party.switch_state takes no arguments";
    }
    if (op != "party.set_active" || a.size() != 1 || !a.contains("character") ||
        !a["character"].is_number_integer() || a["character"].get<double>() < 0 || a["character"].get<double>() > 5) {
        return "provide only character, an integer from 0 to 5";
    }
    return {};
}

std::string CheckPartyTarget(const CUserStatus &s, int target) {
    if (s.party_size < 1 || s.party_size > 6 || s.cur_chara < 0 || s.cur_chara >= s.party_size) {
        return "invalid native party state";
    }
    if (target < 0 || target >= s.party_size) {
        return "character has not joined the party";
    }
    const int active = s.cur_chara;
    if (s.hp[active] <= 0 || (s.ailments[active] & AILMENT_KNOCKED_OUT)) {
        return "active character is knocked out";
    }
    if (s.hp[target] <= 0 || (s.ailments[target] & AILMENT_KNOCKED_OUT)) {
        return "selected character is knocked out";
    }
    if (s.res_limit_zone_current >= 0 && s.res_limit_zone_current < 6) {
        return "character switching is blocked by this floor restriction";
    }
    const int slot = s.equipped_weapon_slot[target];
    if (slot < 0 || slot > 10) {
        return "selected character has an invalid equipped slot";
    }
    const int   item = s.chara_weapons[target][slot].item_no;
    const auto *weapon = item >= ITEM_WEAPON_START && item < ITEM_WEAPON_END ? GetWeaponData(item) : nullptr;
    if (!weapon || weapon->owner != target) {
        return "selected character lacks a valid native equipped weapon";
    }
    return {};
}

bool PartySwitchQueue::Queue(const CUserStatus *s, std::string_view unavailable, std::uint64_t epoch,
                             const std::vector<GameCommand> &commands, int money, const Preflight &preflight, std::string &error) {
    if (commands.size() != 1 || money || commands[0].operation != "party.set_active") {
        error = "party.set_active must be the callback's only engine write (including dependency commands and currency)";
        return false;
    }
    const auto &command = commands[0];
    error = ValidatePartySwitch(command.operation, command.arguments);
    if (!error.empty()) {
        return false;
    }
    if (command.owner.empty()) {
        error = "party switching requires a runtime package owner";
        return false;
    }
    if (pending_) {
        error = "a party switch is already pending for " + pending_->owner;
        return false;
    }
    if (!s || !unavailable.empty()) {
        error = unavailable.empty() ? "game state unavailable" : std::string(unavailable);
        return false;
    }
    const int target = command.arguments.at("character");
    error = CheckPartyTarget(*s, target);
    if (error.empty() && target != s->cur_chara && preflight) {
        error = preflight(target);
    }
    if (!error.empty()) {
        return false;
    }
    Json next = {
        {"state",  "pending"    },
        {"owner",  command.owner},
        {"from",   s->cur_chara },
        {"target", target       },
        {"reason", ""           }
    };
    pending_ = Request{s, command.owner, epoch, s->cur_chara, target};
    state_.swap(next);
    return true;
}

void PartySwitchQueue::Finish(CUserStatus *s, std::string_view unavailable, std::uint64_t epoch,
                              std::span<const std::string> owners, const Preflight &preflight,
                              const std::function<void(int)> &load) {
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
    } else if (s != request.status || !s || s->cur_chara != request.from) {
        error = "native party changed before the request could run";
    } else if (!unavailable.empty()) {
        error = unavailable;
    } else {
        error = CheckPartyTarget(*s, request.target);
    }
    try {
        if (error.empty() && request.target != request.from) {
            if (preflight) {
                error = preflight(request.target);
            }
            if (error.empty()) {
                if (!load) {
                    error = "native character loader unavailable";
                } else {
                    load(request.target);
                    if (s->cur_chara != request.target) {
                        error = "native character loader did not select the requested character";
                    }
                }
            }
        }
    } catch (const std::exception &exception) {
        error = std::string("character switch failed: ") + exception.what();
    } catch (...) {
        error = "character switch failed with an unknown exception";
    }
    state_["state"] = error.empty() ? "complete" : cancelled ? "cancelled"
                                                             : "failed";
    state_["reason"] = error;
}

namespace {
PartySwitchQueue queue;

CUserStatus *Status() { return SaveData ? SaveData->GetDngStatus() : nullptr; }

std::string Unavailable() {
    const auto *s = Status();
    if (mode != GAME_MODE_DUNGEON || gameTask != GAME_TASK_PLAY || BtGameModeFlag != BT_GAME_MODE_PLAY || BtEventMode) {
        return "requires stable dungeon play outside menus, events and first-person view";
    }
    if (!s || s != UserStatus || !NowDngMap || !read_buffer || !CharaFrame || !NowWeapon || !NowWeapon->frame || !NowWeaponHave) {
        return "native character resources are unavailable";
    }
    if (s->cur_chara < 0 || s->cur_chara >= 6 || nowUnitNow != s->cur_chara) {
        return "loaded character is not synchronized";
    }
    const int active = s->cur_chara;
    const int slot = s->equipped_weapon_slot[active];
    if (slot < 0 || slot > 10 || NowWeaponHave->item_no != s->chara_weapons[active][slot].item_no) {
        return "loaded weapon is not synchronized";
    }
    if (BtActStatus.action_on || BtActStatus.movement_locked || BtActStatus.in_presentation || BtActStatus.jumping) {
        return "current action prevents character switching";
    }
    if (s->cur_chara == CHARA_OSMOND && BtActStatus.ground_kind == 10) {
        return "Osmond cannot switch on this ground";
    }
    return CheckPartyTarget(*s, s->cur_chara);
}

std::string Assets(int character) {
    const auto              &s = *Status();
    const int                slot = s.equipped_weapon_slot[character];
    const auto              &weapon = s.chara_weapons[character][slot];
    std::vector<std::string> paths{charaNameTbl[character], "sound/voice/voice" + std::to_string(character) + ".snd"};
    for (int item : {GetDefaultWeaponNo(character), GetDefaultWeaponNo(character) + 1, int(weapon.item_no)}) {
        char model[64]{}, config[64]{};
        BtGetWeaponNamePath3(model, config, item);
        paths.push_back(std::string("commenu/weapon/") + model);
    }
    // Get_Main_EffectPtr also writes Osmond's gun mode; use its retail lookup without that side effect.
    const int element = weapon.best_elem >= 0 && weapon.best_elem <= 4 ? int(weapon.best_elem) : 0;
    int       effect = character < 3 ? character : character == CHARA_RUBY ? 3 + element
                                                                           : 8;
    if (character == CHARA_OSMOND) {
        int entry = 0;
        switch (weapon.item_no) {
            case ITEM_WEAPON_JACKAL:
                entry = 3;
                break;
            case ITEM_WEAPON_G_CRUSHER:
                entry = 4;
                break;
            case ITEM_WEAPON_HEXA_BLASTER:
                entry = 1;
                break;
            case ITEM_WEAPON_STAR_BREAKER:
                entry = 5;
                break;
            case ITEM_WEAPON_SUPERNOVA:
            case ITEM_WEAPON_SNAIL:
                entry = 2;
                break;
        }
        effect = 9 + entry;
    }
    const auto *descriptor = MyEffectEntry_Tbl[effect];
    if (!descriptor) {
        return "native effect definition unavailable";
    }
    paths.push_back(std::string("dun/mainchara/wep_eff/") + descriptor->model_name + ".chr");
    char menu[128]{};
    GetPathReadDifferntLang(menu);
    paths.push_back(std::string(menu) + "itemlst.img");
    for (const auto &path : paths) {
        if (!DataFileAvailable(path)) {
            return "required character resource is unavailable: " + path;
        }
    }
    return {};
}
} // namespace

Json ReadPartySwitchState() {
    const auto *s = Status();
    const auto  reason = Unavailable();
    Json        members = Json::array();
    if (s) {
        for (int i = 0; i < std::clamp(int(s->party_size), 0, 6); ++i) {
            const auto why = reason.empty() ? CheckPartyTarget(*s, i) : reason;
            members.push_back({
                {"character", i                                                  },
                {"allowed",   why.empty() && !queue.Pending()                    },
                {"reason",    queue.Pending() ? "another switch is pending" : why}
            });
        }
    }
    return {
        {"ready",     reason.empty() && !queue.Pending()                    },
        {"reason",    queue.Pending() ? "another switch is pending" : reason},
        {"character", s ? int(s->cur_chara) : -1                            },
        {"members",   members                                               },
        {"request",   queue.State()                                         }
    };
}

bool QueuePartySwitch(const std::vector<GameCommand> &commands, int money, std::string &error) {
    return queue.Queue(Status(), Unavailable(), Scripts().Epoch(), commands, money, Assets, error);
}

void FinishPartySwitch() {
    if (!queue.Pending()) {
        return;
    }
    const auto owners = Scripts().ActiveOwners();
    queue.Finish(Status(), Unavailable(), Scripts().Epoch(), owners, Assets, [](int target) { selectChrUnit(target, 0); });
}
} // namespace mods
