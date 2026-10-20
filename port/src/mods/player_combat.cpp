#include "player_combat.hpp"

#include <algorithm>

#include "btactstatus.hpp"
#include "character.hpp"
#include "dun/gameloop.hpp"
#include "main.hpp"
#include "savedata.hpp"

extern s32 mode;

namespace mods {
namespace {
std::string Ready(const CSaveData *save) {
    if (mode != GAME_MODE_DUNGEON) {
        return "requires an active dungeon";
    }
    if (!save || SaveData != save || UserStatus != static_cast<CUserStatus *>(SaveData->GetDngStatus()) ||
        !NowMonstorUnit || !CharaMain.frame || !BtAllDrawFlag) {
        return "native dungeon player resources are unavailable";
    }
    if (UserStatus->party_size < 1 || UserStatus->party_size > 6 || UserStatus->cur_chara < 0 ||
        UserStatus->cur_chara >= UserStatus->party_size) {
        return "invalid native party state";
    }
    return {};
}
} // namespace

std::string ValidatePlayerCombat(std::string_view op, const nlohmann::json &args) {
    if (!args.is_object()) {
        return "arguments must be an object";
    }
    if (op == "player.combat_state") {
        return args.empty() ? "" : "player.combat_state takes no arguments";
    }
    if (op != "player.invincible" || args.size() != 1 || !args.contains("frames") ||
        !args["frames"].is_number_integer() || args["frames"].get<double>() < 1 || args["frames"].get<double>() > 600) {
        return "provide only frames, an integer from 1 to 600";
    }
    return {};
}

nlohmann::json ReadPlayerCombat() {
    const auto     reason = Ready(SaveData);
    nlohmann::json result{
        {"ready",  reason.empty()},
        {"reason", reason        }
    };
    if (reason.empty()) {
        result["character"] = int(UserStatus->cur_chara);
        result["invincible_frames"] = BtActStatus.invincible_frames;
        result["recoil_frames"] = BtActStatus.recoil_frames;
        result["action_gauge"] = BtActStatus.action_gauge;
    }
    return result;
}

bool PlayerCombatTransaction::Stage(CSaveData &save, std::span<const GameCommand> commands, std::string &error) {
    frames_ = 0;
    int requested = 0;
    for (const auto &command : commands) {
        if (command.operation != "player.invincible") {
            continue;
        }
        error = ValidatePlayerCombat(command.operation, command.arguments);
        if (!error.empty()) {
            return false;
        }
        if (command.owner.empty()) {
            error = "player invincibility requires a runtime package owner";
            return false;
        }
        requested = std::max(requested, command.arguments["frames"].get<int>());
    }
    if (requested) {
        error = Ready(&save);
        if (!error.empty()) {
            return false;
        }
    }
    frames_ = requested;
    return true;
}

void PlayerCombatTransaction::Commit() noexcept {
    if (frames_) {
        BtActStatus.invincible_frames = std::max(BtActStatus.invincible_frames, frames_);
        frames_ = 0;
    }
}
} // namespace mods
