#include "sound_effect.h"

#include <algorithm>
#include <optional>

#include "api.hpp"
#include "dngstatusdata.hpp"
#include "dun/gameloop.hpp"
#include "editloop.hpp"
#include "editpartsinfo.hpp"
#include "entities.hpp"
#include "entity_tracking.hpp"
#include "gamemode.hpp"
#include "georama.hpp"
#include "main.hpp"
#include "runtime.hpp"
#include "savedata.hpp"
#include "snd.hpp"
extern s32 mode;

namespace mods {
namespace {
struct LoadedGeorama {
    GeoramaState                     state;
    std::array<EDITPARTS_INFO *, 24> plots{};

    LoadedGeorama() {
        if (mode != GAME_MODE_EDIT || NowEditMap < 0 || NowEditMap > 5) {
            return;
        }
        state.town = NowEditMap;
        for (int i = 0; i < 24; ++i) {
            auto *plot = EditPartsInfo.GetPartsInfo(i);
            if (!plot || plot->parts_no != i) {
                continue;
            }
            plots[i] = plot;
            state.plots[i].available = true;
            for (int j = 0; j < 6; ++j) {
                const auto &element = plot->elements[j];
                state.plots[i].elements[j] = {element.id, element.required_element, element.enabled != 0};
            }
        }
    }

    void Commit() const {
        // Captured and committed synchronously on the game thread. No mod callback
        // or scene transition can run between capture, validation and publication.
        for (int i = 0; i < 24; ++i) {
            if (!plots[i]) {
                continue;
            }
            for (int j = 0; j < 6; ++j) {
                auto &element = plots[i]->elements[j];
                if ((element.enabled != 0) != state.plots[i].elements[j].enabled) {
                    element.enabled = state.plots[i].elements[j].enabled ? 1 : 0;
                }
            }
        }
    }
};

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
    static std::uint64_t entity_epoch = 0;
    if (entity_epoch != Scripts().Epoch()) {
        EnemyDeaths().Enable(false);
        StopTrackingEntities();
        entity_epoch = Scripts().Epoch();
    }
    if (!Scripts().HasGameplay()) {
        EnemyDeaths().Enable(false);
        StopTrackingEntities();
        return;
    }
    const bool playing = SaveData && (mode == GAME_MODE_EDIT || mode == GAME_MODE_DUNGEON);
    ExpireEntitiesOutsideUnit(playing && mode == GAME_MODE_DUNGEON ? NowMonstorUnit : nullptr);
    if (!playing || mode != GAME_MODE_DUNGEON) {
        StopTrackingEntities();
    }
    World world{mode, MapNo, playing ? SaveData->GetDngStatus()->money : 0, playing};
    if (playing && mode == GAME_MODE_DUNGEON) {
        world.dungeon = SaveData->GetDngStatus()->cur_georama;
        world.floor = SaveData->GetDngStatus()->cur_floor;
    }
    std::vector<EnemyDeath> deaths;
    for (const auto &death : EnemyDeaths().Pending()) {
        if (playing && mode == GAME_MODE_DUNGEON && death.unit == NowMonstorUnit) {
            deaths.push_back(death);
        }
    }
    const auto dropped_deaths = EnemyDeaths().Dropped();
    EnemyDeaths().Reset();
    Scripts().Tick(world, [playing](int amount) {
        if (!playing || !SaveData) {
            return false;
        }
        auto *status = SaveData->GetDngStatus();
        status->money = std::clamp(static_cast<int>(status->money) + amount, 0, 65535);
        return true; }, ReadSnapshot, [world](std::string_view operation, const nlohmann::json &arguments) {
        if (operation.starts_with("entities.")) {
            return ReadEntities(world.playing && world.mode == GAME_MODE_DUNGEON ? NowMonstorUnit : nullptr, operation, arguments);
        }
        if (!SaveData || !world.playing){ throw std::runtime_error("game state unavailable");
}
        if (operation == "combat.get") {
            return ReadCombat(world.mode == GAME_MODE_DUNGEON ? NowMonstorUnit : nullptr, *SaveData->GetDngStatus());
        }
        if (operation == "georama.elements_get") {
            const LoadedGeorama scene;
            return ReadSave(*SaveData, world, operation, arguments, &scene.state);
        }
        return ReadSave(*SaveData, world, operation, arguments); }, [playing](const std::vector<GameCommand> &commands, int money, std::string &error) {
        if (!playing || !SaveData) { error = "game state unavailable"; return false; }
        for (const auto &command : commands) {
            if ((command.operation == "combat.damage" || command.operation == "player.status_add" || command.operation == "player.status_remove") &&
                UserStatus != static_cast<CUserStatus *>(SaveData->GetDngStatus())) {
                error = "native active-character status is unavailable";
                return false;
            }
        }
        std::optional<LoadedGeorama> georama;
        if (std::ranges::any_of(commands, [](const auto &command) { return command.operation == "georama.element_set"; })) {
            georama.emplace();
        }
        CommitEffects effects;
        if (!ApplyEntityAndSave(*SaveData, mode == GAME_MODE_DUNGEON ? NowMonstorUnit : nullptr,
                               commands, money, error, &effects, georama ? &georama->state : nullptr)) { return false; }
        if (georama){ georama->Commit();
}
        for (unsigned i = 0; i < effects.status_sounds; ++i) { SndSePlay(SE_STATUS_AILMENT, -1, 0); }
        return true; }, deaths, dropped_deaths);
    EnemyDeaths().Enable(playing && mode == GAME_MODE_DUNGEON && Scripts().WantsEnemyDeaths());
}
} // namespace mods
