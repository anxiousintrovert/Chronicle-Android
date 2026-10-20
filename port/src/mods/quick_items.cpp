#include "quick_items.hpp"

#include "btactstatus.hpp"
#include "character.hpp"
#include "dun/gameloop.hpp"
#include "main.hpp"
#include "mainitemmodel.hpp"
#include "savedata.hpp"

extern s32 mode;
extern s32 gameTask;
extern int itemNowSel;

namespace mods {
namespace {
std::string Ready(CSaveData *save) {
    if (mode != GAME_MODE_DUNGEON || gameTask != GAME_TASK_PLAY || BtGameModeFlag != BT_GAME_MODE_PLAY || BtEventMode) {
        return "requires ordinary dungeon play outside events";
    }
    if (!save || save != SaveData || UserStatus != static_cast<CUserStatus *>(save->GetDngStatus()) ||
        !NowMonstorUnit || !CharaMain.frame || !BtAllDrawFlag || !activeItem.models) {
        return "native dungeon item resources are unavailable";
    }
    if (UserStatus->party_size < 1 || UserStatus->party_size > 6 || UserStatus->cur_chara < 0 || UserStatus->cur_chara >= UserStatus->party_size) {
        return "invalid native party state";
    }
    if (BtActStatus.action_on || BtActStatus.action_busy || BtActStatus.movement_locked || !BtActStatus.can_act) {
        return "player action prevents quick-slot selection";
    }
    return {};
}

std::string SlotReady(const CUserStatus &status, int slot) {
    const int item = status.item_pack.quick_item_slot[slot - 1];
    const int quantity = status.item_pack.quick_item_qty[slot - 1];
    if (item < ITEM_DUNGEON_START || item >= ITEM_WEAPON_START || quantity < 1 || quantity > 9 || !GetItemData(item)) {
        return "quick slot is empty or invalid";
    }
    if (activeItem.item[slot] != item) {
        return "saved quick item does not match the loaded item";
    }
    const int model = activeItem.model[slot];
    if (model < 0 || model >= 16) {
        return "quick-item model is not loaded";
    }
    const auto &pool = *activeItem.models;
    const int   cache = pool.model_cash[model];
    if (pool.model[model] != ITEM_MODEL_LOADED || cache < 0 || cache >= 6 || !pool.cash[cache] || pool.cash_item[cache] != item) {
        return "quick-item model cache is not synchronized";
    }
    return {};
}
} // namespace

std::string ValidateQuickItems(std::string_view operation, const nlohmann::json &a) {
    if (!a.is_object()) {
        return "arguments must be an object";
    }
    if (operation == "items.quick_slots") {
        return a.empty() ? "" : "items.quick_slots takes no arguments";
    }
    if (operation != "items.quick_slot_select" || a.size() != 1 || !a.contains("slot") || !a["slot"].is_number_integer() ||
        a["slot"].get<double>() < 1 || a["slot"].get<double>() > 3) {
        return "provide only slot, an integer from 1 to 3";
    }
    return {};
}

nlohmann::json ReadQuickItems() {
    const auto     reason = Ready(SaveData);
    nlohmann::json result{
        {"ready",  reason.empty()         },
        {"reason", reason                 },
        {"slots",  nlohmann::json::array()}
    };
    if (!SaveData) {
        return result;
    }
    const auto &status = *SaveData->GetDngStatus();
    for (int slot = 1; slot <= 3; ++slot) {
        const auto blocked = reason.empty() ? SlotReady(status, slot) : reason;
        result["slots"].push_back({
            {"slot",     slot                                      },
            {"item",     status.item_pack.quick_item_slot[slot - 1]},
            {"quantity", status.item_pack.quick_item_qty[slot - 1] },
            {"ready",    blocked.empty()                           },
            {"reason",   blocked                                   }
        });
    }
    if (reason.empty()) {
        result["selected"] = itemNowSel;
    }
    return result;
}

std::string ValidateQuickSelection(CSaveData &save, const CUserStatus &staged, int slot) {
    if (slot < 1 || slot > 3) {
        return "invalid quick slot";
    }
    auto reason = Ready(&save);
    if (!reason.empty()) {
        return reason;
    }
    if (staged.cur_chara != UserStatus->cur_chara || staged.party_size != UserStatus->party_size) {
        return "native party changed during selection";
    }
    return SlotReady(staged, slot);
}

void CommitQuickSelection(int slot) noexcept {
    itemNowSel = slot;
    activeItem.now = slot;
}
} // namespace mods
