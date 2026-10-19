#include <algorithm>

#include "api.hpp"
#include "dngstatusdata.hpp"
#include "gamemode.hpp"
#include "main.hpp"
#include "runtime.hpp"
#include "savedata.hpp"
extern s32 mode;

namespace mods {
namespace {
nlohmann::json Character(const CUserStatus &status, int id) {
    return {
        {"id",          id                             },
        {"hp",          status.hp[id]                  },
        {"max_hp",      status.max_hp[id]              },
        {"defense",     status.defense[id]             },
        {"water",       status.water_now[id]           },
        {"max_water",   status.water_max[id]           },
        {"ailments",    status.ailments[id]            },
        {"weapon_slot", status.equipped_weapon_slot[id]}
    };
}

// Called only in response to a permitted Lua query; never mutates native state.
nlohmann::json ReadSnapshot(std::string_view service) {
    if (!SaveData || (mode != GAME_MODE_EDIT && mode != GAME_MODE_DUNGEON)) {
        return nullptr;
    }
    const auto &status = *SaveData->GetDngStatus();
    if (service == "player") {
        if (status.cur_chara < 0 || status.cur_chara >= 6) {
            return nullptr;
        }
        return Character(status, status.cur_chara);
    }
    if (service == "party") {
        nlohmann::json members = nlohmann::json::array();
        for (int i = 0; i < std::clamp(static_cast<int>(status.party_size), 0, 6); ++i) {
            members.push_back(Character(status, i));
        }
        return {
            {"active_id", status.cur_chara},
            {"members",   members         }
        };
    }
    if (service == "inventory") {
        const auto    &pack = status.item_pack;
        nlohmann::json items = nlohmann::json::array(), quick = nlohmann::json::array();
        const int      capacity = std::clamp(static_cast<int>(pack.num), 0, 103);
        for (int i = 0; i < capacity; ++i) {
            items.push_back({
                {"slot",   i               },
                {"id",     pack.item[i]    },
                {"volume", pack.item_vol[i]}
            });
        }
        for (int i = 0; i < 3; ++i) {
            quick.push_back({
                {"slot",     i                      },
                {"id",       pack.quick_item_slot[i]},
                {"quantity", pack.quick_item_qty[i] }
            });
        }
        return {
            {"capacity",    capacity},
            {"slots",       items   },
            {"quick_slots", quick   }
        };
    }
    return nullptr;
}
} // namespace

void TickGame() {
    if (!Scripts().HasGameplay()) {
        return;
    }
    const bool playing = SaveData && (mode == GAME_MODE_EDIT || mode == GAME_MODE_DUNGEON);
    World      world{mode, MapNo, playing ? SaveData->GetDngStatus()->money : 0, playing};
    if (playing && mode == GAME_MODE_DUNGEON) {
        world.dungeon = SaveData->GetDngStatus()->cur_georama;
        world.floor = SaveData->GetDngStatus()->cur_floor;
    }
    Scripts().Tick(world, [playing](int amount) {
        if (!playing || !SaveData) {
            return false;
        }
        auto *status = SaveData->GetDngStatus();
        status->money = std::clamp(static_cast<int>(status->money) + amount, 0, 65535);
        return true; }, ReadSnapshot, [world](std::string_view operation, const nlohmann::json &arguments) {
        if (!SaveData || !world.playing){ throw std::runtime_error("game state unavailable");
}
        return ReadSave(*SaveData, world, operation, arguments); }, [playing](const std::vector<GameCommand> &commands, int money, std::string &error) {
        if (!playing || !SaveData) { error = "game state unavailable"; return false; }
        return ApplySave(*SaveData, commands, money, error); });
}
} // namespace mods
