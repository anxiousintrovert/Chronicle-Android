#include "sound_effect.h"

#include <algorithm>
#include <optional>

#include "api.hpp"
#include "character.hpp"
#include "dngstatusdata.hpp"
#include "dun/gameloop.hpp"
#include "editloop.hpp"
#include "editmapscript.hpp"
#include "editpartsinfo.hpp"
#include "entities.hpp"
#include "entity_tracking.hpp"
#include "gamemode.hpp"
#include "georama.hpp"
#include "main.hpp"
#include "menu_dungeon.hpp"
#include "menu_misc.hpp"
#include "runtime.hpp"
#include "savedata.hpp"
#include "shops.hpp"
#include "party.hpp"
#include "town.hpp"
#include "player_tuning_native.hpp"
#include "player_combat.hpp"
#include "weapon_loadout.hpp"
#include "quick_items.hpp"
#include "world_transition.hpp"
#include "snd.hpp"
extern s32 mode;
extern s32 gameTask;

namespace mods {
namespace {
bool ShopTablesSafe() { return mode != GAME_MODE_EDIT || GameMode == ED_MODE_WALK; }

bool ShopWritesReady() {
    return SaveData && ((mode == GAME_MODE_EDIT && GameMode == ED_MODE_WALK) ||
                        (mode == GAME_MODE_DUNGEON && (gameTask == GAME_TASK_PLAY || gameTask == GAME_TASK_EYE_CAMERA) && BtEventMode == 0));
}

std::optional<EquipmentContext> CurrentEquipment() {
    if (mode != GAME_MODE_DUNGEON || !SaveData ||
        (gameTask != GAME_TASK_PLAY && gameTask != GAME_TASK_EYE_CAMERA) || BtEventMode != 0 ||
        UserStatus != static_cast<CUserStatus *>(SaveData->GetDngStatus()) ||
        !NowWeaponHave || !NowWeapon || !NowWeapon->frame) {
        return {};
    }
    const auto &s = *SaveData->GetDngStatus();
    const int   character = s.cur_chara;
    if (character < 0 || character >= std::clamp(static_cast<int>(s.party_size), 0, 6)) {
        return {};
    }
    const int slot = s.equipped_weapon_slot[character];
    if (slot < 0 || slot > 10) {
        return {};
    }
    const auto &weapon = s.chara_weapons[character][slot];
    if (weapon.item_no < ITEM_WEAPON_START || weapon.item_no >= ITEM_WEAPON_END ||
        NowWeaponHave->item_no != weapon.item_no || NowWeaponHave->best_elem != weapon.best_elem) {
        return {};
    }
    return EquipmentContext{character, weapon.item_no, weapon.best_elem};
}

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
    RetainQuickUse();
    RetainPlayerTuningOwners();
    static std::uint64_t shop_epoch = 0;
    if (HasOwnedShops() && ShopTablesSafe()) {
        if (shop_epoch != Scripts().Epoch()) {
            RetainOwnedShops({});
        } else {
            RetainOwnedShops(Scripts().ActiveOwners());
        }
    }
    if (!HasOwnedShops()) {
        shop_epoch = Scripts().Epoch();
    }
    static std::uint64_t entity_epoch = 0;
    if (entity_epoch != Scripts().Epoch()) {
        RetainOwnedEnemyScales({});
        RetainOwnedEnemyStats({});
        EnemyDeaths().Enable(false);
        StopTrackingEntities();
        entity_epoch = Scripts().Epoch();
    }
    if (!Scripts().HasGameplay()) {
        RetainOwnedEnemyScales({});
        RetainOwnedEnemyStats({});
        EnemyDeaths().Enable(false);
        StopTrackingEntities();
        return;
    }
    const bool playing = SaveData && (mode == GAME_MODE_EDIT || mode == GAME_MODE_DUNGEON);
    ExpireEntitiesOutsideUnit(playing && mode == GAME_MODE_DUNGEON ? NowMonstorUnit : nullptr);
    if (!playing || mode != GAME_MODE_DUNGEON) {
        RetainOwnedEnemyScales({});
        RetainOwnedEnemyStats({});
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
        if (operation.starts_with("shops.")) {
            auto result = ReadShop(operation, arguments);
            result["writable"] = ShopWritesReady();
            return result;
        }
        if (operation == "weapons.equip_state") return ReadWeaponEquipState();
        if (operation == "player.combat_state") return ReadPlayerCombat();
        if (operation == "items.quick_slots") return ReadQuickItems();
        if (operation == "world.locations") return ReadWorldLocations();
        if (operation == "world.transition_state") return ReadWorldTransition();
        if (operation == "player.tuning_state") return OwnedPlayerTuning().State();
        if (operation == "party.town_state") return ReadTownSelection();
        if (operation == "party.switch_state") return ReadPartySwitchState();
        if (operation == "weapons.refresh_state") {
            const auto equipment = CurrentEquipment();
            nlohmann::json result = {{"ready", equipment.has_value()},
                {"reason", equipment ? "" : "requires stable dungeon play and a synchronized loaded weapon"}};
            if (equipment) {
                result["character"] = equipment->character;
                result["item"] = equipment->item;
                result["element"] = equipment->element;
                result["slot"] = SaveData->GetDngStatus()->equipped_weapon_slot[equipment->character];
                result["cached"] = {{"level", NowWeaponHave->level}, {"attack", NowWeaponHave->attack},
                    {"magic", NowWeaponHave->magic}, {"endurance", NowWeaponHave->endurance},
                    {"speed", NowWeaponHave->speed}, {"max_durability", NowWeaponHave->durability},
                    {"flags", NowWeaponHave->flags}};
            }
            return result;
        }
        if (operation.starts_with("entities.") || operation.starts_with("physics.")) {
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
        if (std::ranges::any_of(commands, [](const auto& command) { return command.operation == "world.transition"; })) {
            return QueueWorldTransition(commands, money, error);
        }
        if (std::ranges::any_of(commands, [](const auto& command) { return command.operation == "party.town_set_active"; })) {
            return QueueTownSelection(commands, money, error);
        }
        if (std::ranges::any_of(commands, [](const auto& command) { return command.operation == "party.set_active"; })) {
            return QueuePartySwitch(commands, money, error);
        }
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
        std::optional<EquipmentContext> equipment;
        if (std::ranges::any_of(commands, [](const auto &command) { return command.operation.starts_with("weapons."); })) {
            equipment = CurrentEquipment();
        }
        const bool town_loadout = std::ranges::any_of(commands, [](const auto &command) { return command.operation == "weapons.equip"; }) && TownLoadoutReady();
        CommitEffects effects;
        if (!ApplyEntityAndSave(*SaveData, mode == GAME_MODE_DUNGEON ? NowMonstorUnit : nullptr,
                               commands, money, error, &effects, georama ? &georama->state : nullptr, equipment ? &*equipment : nullptr, ShopWritesReady(), town_loadout)) { return false; }
        if (georama){ georama->Commit();
}
        if (effects.refresh_weapon) {
            SetWeaponAttachStatus(NowWeaponHave);
            SetWeaponColor();
            auto &s = *SaveData->GetDngStatus();
            const int character = s.cur_chara;
            const int slot = s.equipped_weapon_slot[character];
            MenuWeaponSpSet(NowWeapon, &s.chara_weapons[character][slot]);
        }
        for (unsigned i = 0; i < effects.status_sounds; ++i) { SndSePlay(SE_STATUS_AILMENT, -1, 0); }
        return true; }, deaths, dropped_deaths);
    RetainPlayerTuningOwners();
    RetainQuickUse();
    if (HasOwnedShops() && ShopTablesSafe()) {
        RetainOwnedShops(Scripts().ActiveOwners());
    }
    EnemyDeaths().Enable(playing && mode == GAME_MODE_DUNGEON && Scripts().WantsEnemyDeaths());
    if (!OwnedEnemyStats().empty() || !OwnedEnemyScales().empty()) {
        const auto owners = Scripts().ActiveOwners();
        RetainOwnedEnemyStats(owners);
        RetainOwnedEnemyScales(owners);
    }
}
} // namespace mods
