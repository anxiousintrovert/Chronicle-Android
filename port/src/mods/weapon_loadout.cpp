#include "weapon_loadout.hpp"

#include "character.hpp"
#include "editloop.hpp"
#include "editmapscript.hpp"
#include "main.hpp"
#include "savedata.hpp"

extern s32 mode;
extern int start_event_no, start_system_event;

namespace mods {
namespace {
std::string Unavailable() {
    if (mode != GAME_MODE_EDIT || GameMode != ED_MODE_WALK || EdInteriorFlag || MapNo < 0 || MapNo >= 6 ||
        start_event_no > 0 || start_system_event > 0) {
        return "requires outdoor town walking outside menus and events";
    }
    if (!SaveData || !pEditGround || !MainChara.frame) {
        return "native town resources are unavailable";
    }
    const auto &s = *SaveData->GetDngStatus();
    if (s.party_size < 1 || s.party_size > 6 || s.cur_chara < 0 || s.cur_chara >= s.party_size) {
        return "invalid native party state";
    }
    return {};
}
} // namespace

bool TownLoadoutReady() { return Unavailable().empty(); }

nlohmann::json ReadWeaponEquipState() {
    const auto reason = Unavailable();
    auto       result = nlohmann::json{
        {"ready",  reason.empty()         },
        {"reason", reason                 },
        {"slots",  nlohmann::json::array()}
    };
    if (SaveData) {
        const auto &s = *SaveData->GetDngStatus();
        if (s.party_size >= 1 && s.party_size <= 6) {
            for (int character = 0; character < s.party_size; ++character) {
                const int slot = s.equipped_weapon_slot[character];
                result["slots"].push_back({
                    {"character", character                                                                   },
                    {"slot",      slot                                                                        },
                    {"item",      slot >= 0 && slot <= 10 ? int(s.chara_weapons[character][slot].item_no) : -1}
                });
            }
        }
    }
    return result;
}
} // namespace mods
