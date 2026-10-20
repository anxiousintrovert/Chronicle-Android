#include "api.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <set>

#include "entities.hpp"
#include "georama.hpp"
#include "retail_inventory.hpp"
#include "savedata.hpp"
#include "shops.hpp"
#include "party.hpp"
#include "town.hpp"
#include "player_tuning.hpp"
#include "player_combat.hpp"
#include "quick_items.hpp"
#include "userstatus.hpp"

namespace mods {
using Json = nlohmann::json;

namespace {
constexpr Operation operations[] = {
    {"items.quick_slots", "game.read", "Read saved quick slots and native selection readiness.", false},
    {"items.quick_slot_select", "game.write", "Select a loaded native quick-item slot during dungeon play.", true},
    {"items.quick_slot_use", "game.write", "Request bounded native Square-button use of a loaded quick slot.", true},
    {"weapons.equip", "game.write", "Select an owned retail weapon slot for the saved town loadout.", true},
    {"weapons.equip_state", "game.read", "Read town loadout readiness and equipped slots.", false},
    {"player.combat_state", "game.read", "Read current dungeon player timers, action gauge and readiness.", false},
    {"player.invincible", "game.write", "Grant one-shot native invincibility without shortening an existing timer.", true},
    {"player.tuning_state", "game.read", "Inspect effective dungeon player tuning and per-field owners.", false},
    {"player.tuning_set", "game.write", "Own selected dungeon player tuning controls until cleared or disabled.", true},
    {"player.tuning_clear", "game.write", "Release this package's player tuning controls.", true},
    {"party.town_state", "game.read", "Inspect town selection ownership, readiness and restoration.", false},
    {"party.town_set_active", "game.write", "Own the visible retail town character until released or disabled.", true},
    {"party.switch_state", "game.read", "Inspect native party-switch readiness and the last request.", false},
    {"party.set_active", "game.write", "Request a native character switch after the current frame.", true},
    {"shops.list",                "game.read",     "List native shop lineups and shared base prices.",                             false},
    {"shops.get",                 "game.read",     "Inspect one native shop and its current resource owners.",                     false},
    {"shops.set_items",           "game.write",    "Own a native shop lineup of up to 20 retail goods.",                           true },
    {"shops.register_item",       "game.write",    "Append one retail good to an owned shop lineup.",                              true },
    {"shops.remove_item",         "game.write",    "Remove the first matching good from an owned shop lineup.",                    true },
    {"shops.reset_items",         "game.write",    "Release this package's shop lineup with conditional restoration.",             true },
    {"shops.set_price",           "game.write",    "Own an item's shared native base buy and sell prices.",                        true },
    {"shops.reset_price",         "game.write",    "Release this package's shared item price override.",                           true },
    {"services.query",            "game.read",     "Invoke a dependency query export without managed or game writes.",             false},
    {"services.command",          "game.write",    "Invoke a dependency's command within the caller's transaction.",               true },
    {"events.emit",               "game.write",    "Queue a package-owned notification after commit for next tick.",               true },
    {"physics.query",             "game.read",     "Find active revealed enemy origins inside a sphere (not collision geometry).", false},
    {"physics.velocity_get",      "game.read",     "Read native enemy movement intent before collision.",                          false},
    {"physics.velocity_set",      "game.write",    "Queue one-shot enemy movement intent, replacing knockback.",                   true },
    {"entities.scale_set",        "game.write",    "Own a live enemy model and shadow scale multiplier from 0.5 to 3.0.",          true },
    {"entities.scale_reset",      "game.write",    "Release this package's visual scale for a live enemy.",                        true },
    {"entities.set_stats",        "game.write",    "Own selected live enemy stats with conditional restoration.",                  true },
    {"entities.reset_stats",      "game.write",    "Release this package's live enemy stat changes.",                              true },
    {"weapons.refresh_state",     "game.read",     "Inspect native equipment readiness before requesting equipped mutations.",     false},
    {"combat.get",                "game.read",     "Read native floor enemy count and recorded kills.",                            false},
    {"combat.damage",             "game.write",    "Apply bounded scripted enemy damage and queue native hit/death processing.",   true },
    {"combat.heal",               "game.write",    "Restore bounded HP to an active live enemy without exceeding native maximum.", true },
    {"combat.status_add",         "game.write",    "Extend an active live enemy's native slow or stop timer.",                     true },
    {"entities.list",             "game.read",     "List live revealed dungeon enemies with session-local handles.",               false},
    {"entities.get",              "game.read",     "Resolve a live enemy handle or report expiration.",                            false},
    {"player.get",                "game.read",     "Active or joined character stats.",                                            false},
    {"player.heal",               "game.write",    "Queue bounded HP restoration.",                                                true },
    {"player.damage",             "game.write",    "Queue bounded HP reduction (not a combat hit).",                               true },
    {"player.set",                "game.write",    "Queue current HP within native maximum.",                                      true },
    {"player.thirst_get",         "game.read",     "Read current and maximum water.",                                              false},
    {"player.thirst_set",         "game.write",    "Queue absolute water or restoration.",                                         true },
    {"party.list",                "game.read",     "Joined characters in native order.",                                           false},
    {"party.active",              "game.read",     "Active character stats.",                                                      false},
    {"inventory.get",             "game.read",     "Bag and quick-use slots.",                                                     false},
    {"weapons.list",              "game.read",     "Owned weapons by character and slot.",                                         false},
    {"weapons.get",               "game.read",     "Owned weapon stats by character and slot.",                                    false},
    {"economy.get",               "game.read",     "Wallet balance and capacity.",                                                 false},
    {"economy.add",               "economy.write", "Queue bounded currency addition.",                                             true },
    {"economy.spend",             "economy.write", "Queue spending; reject the callback if funds are insufficient.",               true },
    {"world.get",                 "game.read",     "Current mode, map, dungeon, floor and restrictions.",                          false},
    {"story.flag_get",            "game.read",     "Global story bit get.",                                                        false},
    {"story.flag_set",            "game.write",    "Global story bit set.",                                                        true },
    {"story.int_get",             "game.read",     "Global signed story integer get.",                                             false},
    {"story.int_set",             "game.write",    "Global signed story integer set.",                                             true },
    {"map.flag_get",              "game.read",     "Map story bit get.",                                                           false},
    {"map.flag_set",              "game.write",    "Map story bit set.",                                                           true },
    {"map.init_flag_get",         "game.read",     "Map initialization bit get.",                                                  false},
    {"map.init_flag_set",         "game.write",    "Map initialization bit set.",                                                  true },
    {"fishing.get",               "game.read",     "Fishing point balance.",                                                       false},
    {"fishing.add",               "economy.write", "Add fishing points within native capacity.",                                   true },
    {"fishing.spend",             "economy.write", "Spend fishing points atomically.",                                             true },
    {"georama.get",               "game.read",     "Saved plots, placements and request completion.",                              false},
    {"georama.elements_get",      "game.read",     "Inspect loaded retail optional plot elements and their prerequisites.",        false},
    {"georama.element_set",       "game.write",    "Enable or disable a loaded retail plot element atomically with saved state.",  true },
    {"dungeon.progress",          "game.read",     "Highest reached floor by dungeon.",                                            false},
    {"player.max_hp_set",         "game.write",    "Set HP capacity and clamp the current gauge.",                                 true },
    {"player.max_water_set",      "game.write",    "Set water capacity and clamp the current gauge.",                              true },
    {"player.defense_set",        "game.write",    "Set joined character base defense.",                                           true },
    {"player.status_get",         "game.read",     "Read native status mask and shared duration counter.",                         false},
    {"player.status_add",         "game.write",    "Apply an active-character ailment using retail rules and durations.",          true },
    {"player.status_remove",      "game.write",    "Remove one active-character ailment; preserve unrelated bits.",                true },
    {"weapons.durability_set",    "game.write",    "Set an owned weapon WHP gauge, without item-use effects.",                     true },
    {"inventory.count",           "game.read",     "Count owned retail bag and quick-slot copies.",                                false},
    {"inventory.give",            "game.write",    "Grant retail items using an explicit local random seed.",                      true },
    {"inventory.remove",          "game.write",    "Remove owned retail items, bag first then quick slots.",                       true },
    {"weapons.give",              "game.write",    "Grant a retail weapon to its native owner using a local seed.",                true },
    {"weapons.abs_get",           "game.read",     "Read owned ABS, native threshold and synthesis points.",                       false},
    {"weapons.abs_add",           "game.write",    "Grant ABS and synthesis points without automatic level-up.",                   true },
    {"weapons.attachments_get",   "game.read",     "Inspect fitted native attachment sockets.",                                    false},
    {"items.native_definition",   "game.read",     "Inspect an existing retail definition; does not register content.",            false},
    {"items.use",                 "game.write",    "Consume owned retail healing, water or ailment-cure items atomically.",         true },
    {"attachments.list",          "game.read",     "Inspect the retail attachment bag and overflow slots.",                        false},
    {"attachments.give",          "game.write",    "Grant existing retail attachments into free regular bag slots.",               true },
    {"attachments.remove",        "game.write",    "Discard one owned attachment by its current bag slot.",                        true },
    {"weapons.build_up_list",     "game.read",     "Inspect native build-up targets and owned weapon requirements without RNG.",   false},
    {"weapons.attachment_set",    "game.write",    "Fit or swap attachments; equipped mutations require native readiness.",        true },
    {"weapons.attachment_remove", "game.write",    "Return attachments to the bag; equipped changes require native readiness.",    true },
    {"weapons.upgrade",           "game.write",    "Level up using ABS or Powder; equipped mutations require native readiness.",   true },
    {"weapons.build_up",          "game.write",    "Evolve a retail weapon; active model changes require a separate loader.",      true },
    {"weapons.synthesize",        "game.write",    "Status-break an unequipped level 5+ weapon into a native SynthSphere.",        true },
};

int CharacterId(const CUserStatus &s, const Json &a) {
    const int id = a.value("character", static_cast<int>(s.cur_chara));
    if (id < 0 || id >= std::clamp(static_cast<int>(s.party_size), 0, 6)) {
        throw std::runtime_error("character is not a joined party member");
    }
    return id;
}

int Ailment(std::string_view name) {
    if (name == "freeze") {
        return AILMENT_FREEZE;
    }
    if (name == "stamina") {
        return AILMENT_STAMINA;
    }
    if (name == "poison") {
        return AILMENT_POISON;
    }
    if (name == "curse") {
        return AILMENT_CURSE;
    }
    if (name == "goo") {
        return AILMENT_GOO;
    }
    return 0;
}

bool AddAilment(CUserStatus &s, int id, int ailment) {
    // BtSetStatusErr's save-field rules, staged without its immediate sound or
    // temporary rebinding of the engine's global UserStatus pointer. Verified
    // against the linked native routine over all retail ailment combinations.
    auto &mask = s.ailments[id];
    bool  sound = false;
    switch (ailment) {
        case AILMENT_FREEZE:
            mask = (mask | ailment) & ~(AILMENT_STAMINA | AILMENT_POISON | AILMENT_GOO);
            s.ailment_frames[id] = 300;
            sound = true;
            break;
        case AILMENT_STAMINA:
            if (!(mask & AILMENT_FREEZE)) {
                mask |= ailment;
                s.ailment_frames[id] = 1800;
            }
            break;
        case AILMENT_POISON:
            if (!(mask & (AILMENT_FREEZE | AILMENT_STAMINA))) {
                mask = (mask | ailment) & ~AILMENT_GOO;
                sound = true;
            }
            break;
        case AILMENT_CURSE:
            mask |= ailment;
            sound = true;
            break;
        case AILMENT_GOO:
            if (!(mask & (AILMENT_FREEZE | AILMENT_STAMINA | AILMENT_POISON))) {
                mask |= ailment;
                sound = true;
            }
            break;
    }
    if (!(mask & ailment)) {
        throw std::runtime_error("native status rules rejected the effect");
    }
    return sound;
}

Json Character(const CUserStatus &s, int id) {
    return {
        {"character",   id                        },
        {"hp",          s.hp[id]                  },
        {"max_hp",      s.max_hp[id]              },
        {"defense",     s.defense[id]             },
        {"water",       s.water_now[id]           },
        {"max_water",   s.water_max[id]           },
        {"ailments",    s.ailments[id]            },
        {"weapon_slot", s.equipped_weapon_slot[id]}
    };
}

Json Weapon(const WEAPON_HAVE &w, int character, int slot) {
    return {
        {"character",      character     },
        {"slot",           slot          },
        {"item",           w.item_no     },
        {"level",          w.level       },
        {"attack",         w.attack      },
        {"endurance",      w.endurance   },
        {"speed",          w.speed       },
        {"magic",          w.magic       },
        {"durability",     w.durability_f},
        {"max_durability", w.durability  },
        {"abs",            w.experience  }
    };
}
} // namespace

std::span<const Operation> Operations() { return operations; }

const Operation *FindOperation(std::string_view name) {
    for (const auto &op : operations) {
        if (op.name == name) {
            return &op;
        }
    }
    return nullptr;
}

std::string ValidateOperation(std::string_view name, const Json &a) {
    if (!FindOperation(name)) {
        return "unsupported host operation";
    }
    if (name == "weapons.equip_state") return a.is_object() && a.empty() ? "" : "weapons.equip_state takes no arguments";
    if (name == "player.combat_state" || name == "player.invincible") return ValidatePlayerCombat(name, a);
    if (name == "items.quick_slots" || name == "items.quick_slot_select" || name == "items.quick_slot_use") return ValidateQuickItems(name, a);
    if (name == "player.tuning_state") return a.is_object() && a.empty() ? "" : "player.tuning_state takes no arguments";
    if (PlayerTuning::IsWrite(name)) return PlayerTuning::Validate(name, a);
    if (name.starts_with("shops.")) {
        return ValidateShop(name, a);
    }
    if (name == "party.town_state" || name == "party.town_set_active") return ValidateTownSelection(name, a);
    if (name == "party.switch_state" || name == "party.set_active") return ValidatePartySwitch(name, a);
    if (retail::Handles(name)) {
        return retail::Validate(name, a);
    }
    if (IsEntityWrite(name)) {
        return ValidateEntityWrite(name, a);
    }
    if (name.starts_with("entities.") || name.starts_with("physics.")) {
        return ValidateEntityQuery(name, a);
    }
    if (!a.is_object()) {
        return "arguments must be an object";
    }
    if (name == "services.command" || name == "services.query") {
        if (a.size() != 3 || !a.contains("provider") || !a["provider"].is_string() ||
            !a.contains("name") || !a["name"].is_string() || !a.contains("arguments") || !a["arguments"].is_object()) {
            return "provide provider, name and object arguments only";
        }
        const auto &provider = a["provider"].get_ref<const std::string &>();
        const auto &service = a["name"].get_ref<const std::string &>();
        const auto  key_char = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.'; };
        if (provider.empty() || provider.size() > 96 || provider.front() < 'a' || provider.front() > 'z' ||
            !std::ranges::all_of(provider, [&](char c) { return key_char(c) || c == '-'; }) ||
            service.empty() || service.size() > 64 || !std::ranges::all_of(service, key_char)) {
            return "invalid service identity";
        }
        return {};
    }
    if (name == "events.emit") {
        if (a.size() != 2 || !a.contains("name") || !a["name"].is_string() ||
            !a.contains("payload") || !a["payload"].is_object()) {
            return "provide name and object payload only";
        }
        const auto &event = a["name"].get_ref<const std::string &>();
        if (event.empty() || event.size() > 64 || !std::ranges::all_of(event, [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
            })) {
            return "event name must be 1-64 lowercase letters, digits, underscores or dots";
        }
        return {};
    }
    if (name == "weapons.refresh_state") {
        return a.empty() ? "" : "weapons.refresh_state takes no arguments";
    }
    if (name == "combat.get") {
        return a.empty() ? "" : "combat.get takes no arguments";
    }
    if (name == "player.status_add" || name == "player.status_remove") {
        for (const auto &[key, value] : a.items()) {
            if (key == "status") {
                if (!value.is_string() || !Ailment(value.get_ref<const std::string &>())) {
                    return "status must be freeze, stamina, poison, curse or goo";
                }
            } else if (key == "character") {
                if (!value.is_number_integer() || value.get<double>() < 0 || value.get<double>() > 5) {
                    return "character must be an integer from 0 to 5";
                }
            } else {
                return "unknown status argument: " + key;
            }
        }
        return a.contains("status") ? "" : "status is required";
    }
    if (name.starts_with("story.") || name.starts_with("map.")) {
        const bool map = name.starts_with("map.");
        const bool integer = name.starts_with("story.int_");
        const bool write = name.ends_with("_set");
        for (const auto &[key, value] : a.items()) {
            const bool valid = key == "id" || (map && key == "map") || (write && key == "value");
            if (!valid || !value.is_number_integer()) {
                return "invalid flag argument: " + key;
            }
            const double n = value.get<double>();
            const double lo = key == "value" && integer ? -2147483648.0 : 0;
            const double hi = key == "map" ? 79 : key == "id" ? (map ? 511 : integer ? 31
                                                                                     : 8191)
                                              : integer       ? 2147483647.0
                                                              : 1;
            if (n < lo || n > hi) {
                return "flag argument out of range: " + key;
            }
        }
        if (!a.contains("id") || (map && !a.contains("map")) || (write && !a.contains("value"))) {
            return "missing flag argument";
        }
        return {};
    }
    if (name == "georama.elements_get" || name == "georama.element_set") {
        return ValidateGeoramaElements(name, a);
    }
    if (name == "georama.get") {
        if (a.size() != 1 || !a.contains("town") || !a["town"].is_number_integer() || a["town"].get<double>() < 0 || a["town"].get<double>() > 5) {
            return "town must be an integer from 0 to 5";
        }
        return {};
    }
    std::set<std::string> allowed;
    if (name.starts_with("player.") || (name == "weapons.get" || name == "weapons.durability_set")) {
        allowed.insert("character");
    }
    if (name == "player.heal" || name == "player.damage" || ((name.starts_with("economy.") || name.starts_with("fishing.")) && !name.ends_with(".get"))) {
        allowed.insert("amount");
    }
    if (name == "player.set") {
        allowed.insert("hp");
    }
    if (name == "player.thirst_set") {
        allowed.insert("water");
        allowed.insert("restore");
    }
    if ((name == "weapons.get" || name == "weapons.durability_set")) {
        allowed.insert("slot");
    }
    if (name == "player.max_hp_set") {
        allowed.insert("max_hp");
    }
    if (name == "player.max_water_set") {
        allowed.insert("max_water");
    }
    if (name == "player.defense_set") {
        allowed.insert("defense");
    }
    if (name == "weapons.durability_set") {
        allowed.insert("durability");
    }
    for (const auto &[key, value] : a.items()) {
        if (!allowed.contains(key)) {
            return "unknown argument: " + key;
        }
        const bool water = key == "water" || key == "restore" || key == "max_water" || key == "durability";
        if ((!water && !value.is_number_integer()) || (water && !value.is_number())) {
            return "invalid number: " + key;
        }
        const double n = value.get<double>();
        const double max = key == "character" ? 5 : key == "slot" ? 10
                                                                  : 32767;
        if (!std::isfinite(n) || n < 0 || n > max) {
            return "argument out of range: " + key;
        }
    }
    for (const auto *key : {"max_hp", "max_water", "defense", "durability"}) {
        if (allowed.contains(key) && !a.contains(key)) {
            return std::string(key) + " is required";
        }
    }
    if ((name == "player.max_hp_set" && a.at("max_hp").get<int>() < 1) || (name == "player.max_water_set" && a.at("max_water").get<float>() < 1)) {
        return "capacity must be at least 1";
    }
    if (allowed.contains("amount") && !a.contains("amount")) {
        return "amount is required";
    }
    if (name == "player.set" && !a.contains("hp")) {
        return "hp is required";
    }
    if ((name == "weapons.get" || name == "weapons.durability_set") && !a.contains("slot")) {
        return "slot is required";
    }
    if (name == "player.thirst_set" && a.contains("water") == a.contains("restore")) {
        return "provide exactly one of water or restore";
    }
    return {};
}

Json ReadStatus(const CUserStatus &s, const World &w, std::string_view op, const Json &a) {
    const auto error = ValidateOperation(op, a);
    if (!error.empty()) {
        throw std::runtime_error(error);
    }
    if (retail::Handles(op)) {
        return retail::Read(s, op, a);
    }
    if (op == "player.get" || op == "party.active") {
        return Character(s, CharacterId(s, a));
    }
    if (op == "player.status_get") {
        const auto id = CharacterId(s, a);
        return {
            {"character", id                  },
            {"mask",      s.ailments[id]      },
            {"frames",    s.ailment_frames[id]}
        };
    }
    if (op == "player.thirst_get") {
        const auto id = CharacterId(s, a);
        return {
            {"character",     id                    },
            {"water",         s.water_now[id]       },
            {"max_water",     s.water_max[id]       },
            {"drain_enabled", !s.water_drain_disable},
            {"step_enabled",  !s.step_disable       }
        };
    }
    if (op == "party.list") {
        Json result = Json::array();
        for (int i = 0; i < std::clamp(static_cast<int>(s.party_size), 0, 6); ++i) {
            result.push_back(Character(s, i));
        }
        return result;
    }
    if (op == "inventory.get") {
        Json      items = Json::array(), quick = Json::array();
        const int capacity = std::clamp(static_cast<int>(s.item_pack.num), 0, 103);
        for (int i = 0; i < capacity; ++i) {
            items.push_back({
                {"slot",   i                      },
                {"item",   s.item_pack.item[i]    },
                {"volume", s.item_pack.item_vol[i]}
            });
        }
        for (int i = 0; i < 3; ++i) {
            quick.push_back({
                {"slot",     i                             },
                {"item",     s.item_pack.quick_item_slot[i]},
                {"quantity", s.item_pack.quick_item_qty[i] }
            });
        }
        return {
            {"capacity",    capacity},
            {"slots",       items   },
            {"quick_slots", quick   }
        };
    }
    if (op == "weapons.get") {
        const int   id = CharacterId(s, a), slot = a.at("slot").get<int>();
        const auto &weapon = s.chara_weapons[id][slot];
        if (weapon.item_no < ITEM_WEAPON_START) {
            throw std::runtime_error("weapon slot is empty");
        }
        return Weapon(weapon, id, slot);
    }
    if (op == "weapons.list") {
        Json result = Json::array();
        for (int i = 0; i < std::clamp(static_cast<int>(s.party_size), 0, 6); ++i) {
            for (int slot = 0; slot < 11; ++slot) {
                if (s.chara_weapons[i][slot].item_no >= ITEM_WEAPON_START) {
                    result.push_back(Weapon(s.chara_weapons[i][slot], i, slot));
                }
            }
        }
        return result;
    }
    if (op == "economy.get") {
        return {
            {"money",    s.money},
            {"capacity", 65535  }
        };
    }
    if (op == "dungeon.progress") {
        Json result = Json::array();
        for (int i = 0; i < 7; ++i) {
            result.push_back({
                {"dungeon",       i                 },
                {"floor_reached", s.floor_reached[i]}
            });
        }
        return result;
    }
    if (op == "world.get") {
        return {
            {"mode",        w.mode                  },
            {"map",         w.map                   },
            {"dungeon",     s.cur_georama           },
            {"floor",       s.cur_floor             },
            {"restriction", s.res_limit_zone_current}
        };
    }
    throw std::runtime_error("operation is not a query");
}

bool ApplyStatus(CUserStatus &status, const std::vector<GameCommand> &commands, int money_delta, std::string &error, CommitEffects *effects) {
    if (effects) {
        *effects = {};
    }
    CommitEffects pending;
    if (commands.empty() && money_delta == 0) {
        error.clear();
        return true;
    }
    if (commands.size() > 64 || money_delta < -32767 || money_delta > 32767) {
        error = "callback write budget exceeded";
        return false;
    }
    // Only allocated/copied when a callback actually requests a write.
    auto  staged = std::make_unique<CUserStatus>(status);
    auto &s = *staged;
    try {
        s.money = std::clamp(static_cast<int>(s.money) + money_delta, 0, 65535);
        for (const auto &command : commands) {
            const auto &op = command.operation;
            const auto &a = command.arguments;
            error = ValidateOperation(op, a);
            if (!error.empty()) {
                return false;
            }
            if (!FindOperation(op)->writes) {
                error = "query cannot be committed";
                return false;
            }
            if (retail::Handles(op)) {
                retail::Apply(s, op, a);
                continue;
            }
            if (op.starts_with("economy.")) {
                const int amount = a.at("amount");
                if (op == "economy.spend" && amount > s.money) {
                    error = "insufficient currency";
                    return false;
                }
                s.money = std::clamp(s.money + (op == "economy.spend" ? -amount : amount), 0, 65535);
                continue;
            }
            if (op == "weapons.durability_set") {
                auto &weapon = s.chara_weapons[CharacterId(s, a)][a.at("slot").get<int>()];
                if (weapon.item_no < ITEM_WEAPON_START || weapon.item_no >= ITEM_WEAPON_END || weapon.durability < 0) {
                    throw std::runtime_error("invalid owned weapon");
                }
                weapon.durability_f = std::clamp(a.at("durability").get<float>(), 0.0f, static_cast<float>(weapon.durability));
                continue;
            }
            if (!op.starts_with("player.")) {
                throw std::runtime_error("operation requires save host");
            }
            const int id = CharacterId(s, a);
            if (op == "player.status_add" || op == "player.status_remove") {
                if (id != s.cur_chara) {
                    throw std::runtime_error("status changes require the active character");
                }
                const int ailment = Ailment(a.at("status").get_ref<const std::string &>());
                if (op == "player.status_add") {
                    pending.status_sounds += AddAilment(s, id, ailment);
                } else {
                    s.ailments[id] &= ~ailment;
                    // Only freeze/stamina advance the shared native timer.
                    if (!(s.ailments[id] & (AILMENT_FREEZE | AILMENT_STAMINA))) {
                        s.ailment_frames[id] = 0;
                    }
                }
            } else if (op == "player.defense_set") {
                s.defense[id] = a.at("defense").get<int>();
            } else if (op == "player.max_hp_set") {
                s.max_hp[id] = a.at("max_hp").get<int>();
                s.hp[id] = std::clamp(static_cast<int>(s.hp[id]), 0, static_cast<int>(s.max_hp[id]));
                s.next_hp[id] = s.hp[id];
                s.life_step[id] = 0;
            } else if (op == "player.max_water_set") {
                if (!std::isfinite(s.water_now[id])) {
                    throw std::runtime_error("invalid native water gauge");
                }
                s.water_max[id] = a.at("max_water").get<float>();
                s.water_now[id] = std::clamp(s.water_now[id], 0.0f, s.water_max[id]);
                s.drink_next[id] = s.drink_step[id] = 0;
            } else if (op == "player.thirst_set") {
                const float cap = s.water_max[id];
                if (!std::isfinite(cap) || cap < 0 || !std::isfinite(s.water_now[id])) {
                    throw std::runtime_error("invalid native water gauge");
                }
                const float value = a.contains("water") ? a["water"].get<float>() : s.water_now[id] + a["restore"].get<float>();
                s.water_now[id] = std::clamp(value, 0.0f, cap);
                s.drink_next[id] = s.drink_step[id] = 0;
            } else {
                if (s.max_hp[id] < 0) {
                    throw std::runtime_error("invalid native HP capacity");
                }
                const int value = op == "player.set" ? a["hp"].get<int>() : s.hp[id] + (op == "player.damage" ? -1 : 1) * a["amount"].get<int>();
                s.hp[id] = std::clamp(value, 0, static_cast<int>(s.max_hp[id]));
                s.next_hp[id] = s.hp[id];
                s.life_step[id] = 0;
                if (s.hp[id] > 0) {
                    s.ailments[id] &= ~AILMENT_KNOCKED_OUT;
                } else {
                    s.ailments[id] |= AILMENT_KNOCKED_OUT;
                }
            }
        }
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
    status = s;
    if (effects) {
        *effects = pending;
    }
    error.clear();
    return true;
}

Json ReadSave(CSaveData &save, const World &world, std::string_view op, const Json &a, const GeoramaState *georama) {
    const auto error = ValidateOperation(op, a);
    if (!error.empty()) {
        throw std::runtime_error(error);
    }
    if (op == "georama.elements_get") {
        if (!georama) {
            throw std::runtime_error("native Georama scene is unavailable");
        }
        return ReadGeoramaElements(save, *georama, a);
    }
    if (op == "weapons.build_up_list") {
        return retail::Read(*save.GetDngStatus(), op, a, save.GetGameIntFlag(0x14));
    }
    if (op == "story.flag_get") {
        return {
            {"value", save.GetGameFlag(a.at("id"))}
        };
    }
    if (op == "story.int_get") {
        return {
            {"value", save.GetGameIntFlag(a.at("id"))}
        };
    }
    if (op == "map.flag_get") {
        return {
            {"value", save.GetMapFlag(a.at("map"), a.at("id"))}
        };
    }
    if (op == "map.init_flag_get") {
        return {
            {"value", save.GetMapInitFlag(a.at("map"), a.at("id"))}
        };
    }
    if (op == "fishing.get") {
        return {
            {"points",   save.GetFishingPoint()},
            {"capacity", 9999                  }
        };
    }
    if (op == "georama.get") {
        const auto &g = *save.GetGrdData(a.at("town"));
        Json        plots = Json::array(), placements = Json::array(), requests = Json::array();
        for (int i = 0; i < 24; ++i) {
            const auto &p = g.parts_info[i];
            Json        elements = Json::array();
            for (int j = 0; j < 6; ++j) {
                elements.push_back(p.element_enabled[j]);
            }
            plots.push_back({
                {"plot",             i                 },
                {"obtained",         p.obtained        },
                {"placed",           p.placed          },
                {"stock",            p.stock           },
                {"completion_flags", p.completion_flags},
                {"elements",         elements          }
            });
        }
        for (const auto &p : g.placed_parts) {
            if (p.part_id < 0) {
                break;
            }
            placements.push_back({
                {"part",     p.part_id},
                {"rotation", p.rot_y  },
                {"x",        p.pos_x  },
                {"y",        p.pos_y  },
                {"z",        p.pos_z  }
            });
        }
        for (int i = 0; i < std::clamp(g.request_count, 0, 24); ++i) {
            requests.push_back(g.request_complete[i]);
        }
        return {
            {"town",       a.at("town")},
            {"plots",      plots       },
            {"placements", placements  },
            {"requests",   requests    }
        };
    }
    return ReadStatus(*save.GetDngStatus(), world, op, a);
}

bool ApplySave(CSaveData &save, const std::vector<GameCommand> &commands, int money_delta, std::string &error, CommitEffects *effects, GeoramaState *georama, const EquipmentContext *equipment, bool town_loadout) {
    int quick_selection = 0;
    std::string quick_use_owner;
    if (effects) {
        *effects = {};
    }
    CommitEffects pending;
    if (commands.empty() && money_delta == 0) {
        error.clear();
        return true;
    }
    if (commands.size() > 64 || money_delta < -32767 || money_delta > 32767) {
        error = "callback write budget exceeded";
        return false;
    }
    // The copy constructor does not run save initialization or consume native RNG.
    // Only callbacks with writes pay for staging. Native selection is published
    // after the save and resource validations, alongside the save commit.
    auto                        staged = std::make_unique<CSaveData>(save);
    std::optional<GeoramaState> staged_georama;
    if (georama) {
        staged_georama = *georama;
    }
    try {
        if (!ApplyStatus(*staged->GetDngStatus(), {}, money_delta, error)) {
            return false;
        }
        for (const auto &command : commands) {
            const auto &op = command.operation;
            const auto &a = command.arguments;
            error = ValidateOperation(op, a);
            if (!error.empty()) {
                return false;
            }
            if (!FindOperation(op)->writes) {
                error = "query cannot be committed";
                return false;
            }
            if (op == "items.quick_slot_select" || op == "items.quick_slot_use") {
                if (command.owner.empty()) {
                    throw std::runtime_error("quick-slot selection requires a runtime package owner");
                }
                const int slot = a.at("slot");
                if (!quick_use_owner.empty()) throw std::runtime_error("only one quick use and no later selection are allowed per callback");
                error = ValidateQuickSelection(save, *staged->GetDngStatus(), slot);
                if (!error.empty()) return false;
                quick_selection = slot;
                if (op == "items.quick_slot_use") quick_use_owner = command.owner;
            } else if (op == "weapons.equip") {
                if (!town_loadout || equipment) {
                    throw std::runtime_error("equipping requires a ready town loadout context; dungeon resource switching is unavailable");
                }
                retail::Equip(*staged->GetDngStatus(), a);
            } else if (op == "weapons.build_up") {
                retail::BuildUp(*staged, a, equipment != nullptr);
            } else if (op == "weapons.upgrade" || op == "weapons.attachment_set" || op == "weapons.attachment_remove") {
                retail::Apply(*staged->GetDngStatus(), op, a, equipment != nullptr);
            } else if (op == "georama.element_set") {
                if (!staged_georama) {
                    throw std::runtime_error("native Georama scene is unavailable");
                }
                ApplyGeoramaElement(*staged, *staged_georama, a);
            } else if (op == "story.flag_set") {
                staged->SetGameFlag(a.at("id"), a.at("value"));
            } else if (op == "story.int_set") {
                staged->SetGameIntFlag(a.at("id"), a.at("value"));
            } else if (op == "map.flag_set") {
                staged->SetMapFlag(a.at("map"), a.at("id"), a.at("value"));
            } else if (op == "map.init_flag_set") {
                staged->SetMapInitFlag(a.at("map"), a.at("id"), a.at("value"));
            } else if (op.starts_with("fishing.")) {
                const int amount = a.at("amount"), balance = staged->GetFishingPoint();
                if (op == "fishing.spend" && amount > balance) {
                    error = "insufficient fishing points";
                    return false;
                }
                staged->SetFishingPoint(balance + (op == "fishing.spend" ? -amount : amount));
            } else {
                CommitEffects current;
                if (!ApplyStatus(*staged->GetDngStatus(), {command}, 0, error, &current)) {
                    return false;
                }
                pending.status_sounds += current.status_sounds;
            }
        }
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
    if (equipment) {
        const auto &before = *save.GetDngStatus();
        const auto &after = *staged->GetDngStatus();
        const int   character = equipment->character;
        if (character < 0 || character >= std::clamp(static_cast<int>(before.party_size), 0, 6) ||
            before.cur_chara != character || after.cur_chara != character) {
            error = "native equipment context is stale";
            return false;
        }
        const int slot = before.equipped_weapon_slot[character];
        if (slot < 0 || slot > 10 || after.equipped_weapon_slot[character] != slot) {
            error = "active equipment slot changed during transaction";
            return false;
        }
        const auto &old_weapon = before.chara_weapons[character][slot];
        const auto &new_weapon = after.chara_weapons[character][slot];
        if (old_weapon.item_no != equipment->item || old_weapon.best_elem != equipment->element) {
            error = "native weapon cache is not synchronized";
            return false;
        }
        if (new_weapon.item_no != old_weapon.item_no ||
            (character == CHARA_RUBY && new_weapon.best_elem != old_weapon.best_elem)) {
            error = "equipped mutation requires loading a different native model or Ruby effect";
            return false;
        }
        pending.refresh_weapon = std::memcmp(&old_weapon, &new_weapon, sizeof(old_weapon)) != 0;
    }
    if (quick_selection) {
        error = ValidateQuickSelection(save, *staged->GetDngStatus(), quick_selection);
        if (!error.empty()) return false;
    }
    std::optional<QuickUseRequest> quick_use;
    if (!quick_use_owner.empty()) {
        try {
            quick_use = PrepareQuickUse(save, *staged->GetDngStatus(), quick_selection, std::move(quick_use_owner));
        } catch (const std::exception &e) {
            error = e.what();
            return false;
        }
    }
    save = *staged;
    if (quick_selection) CommitQuickSelection(quick_selection);
    if (quick_use) CommitQuickUse(std::move(*quick_use));
    if (georama) {
        *georama = *staged_georama;
    }
    if (effects) {
        *effects = pending;
    }
    error.clear();
    return true;
}
} // namespace mods
