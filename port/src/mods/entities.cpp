#include "entities.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>
#include <stdexcept>

#include "api.hpp"
#include "entity_reactions.hpp"
#include "entity_tracking.hpp"
#include "monstorunit.hpp"
#include "runtime.hpp"
#include "shops.hpp"
#include "loot.hpp"
#include "player_tuning_native.hpp"
#include "player_combat.hpp"
#include "userstatus.hpp"
#include "weaponelement.hpp"

namespace mods {
namespace {
EntityHandles::Handle Parse(std::string_view text) {
    EntityHandles::Handle handle = 0;
    if (text.empty() || text.size() > 20 || text.front() == '0') {
        return 0;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), handle);
    return error == std::errc{} && end == text.data() + text.size() ? handle : 0;
}

bool Live(const MONSTOR &actor) {
    return (actor.state == 1 || actor.state == 2) && actor.hp > 0 && actor.revealed != 0;
}

bool Vector3(const nlohmann::json &value, double limit) {
    if (!value.is_array() || value.size() != 3) {
        return false;
    }
    return std::ranges::all_of(value, [limit](const auto &axis) {
        return axis.is_number() && std::isfinite(axis.template get<double>()) &&
               std::abs(axis.template get<double>()) <= limit;
    });
}

nlohmann::json Velocity(const MONSTOR &actor, const nlohmann::json &entity) {
    if (!Live(actor) || actor.state != 2 || actor.max_hp < actor.hp) {
        throw std::runtime_error("velocity requires an active live enemy with valid health");
    }
    std::array<float, 3> velocity{};
    const char          *source = "stopped";
    if (actor.stop_timer <= 0) {
        if (!std::isfinite(actor.knockback[0]) || !std::isfinite(actor.ground_distance)) {
            throw std::runtime_error("native movement state is not finite");
        }
        const bool  impulse = actor.knockback[0] > 0;
        const float speed = impulse ? actor.knockback[0] : actor.movement_speed;
        const auto *direction = impulse ? actor.knockback_direction : actor.movement;
        if (!std::isfinite(speed) || speed < 0) {
            throw std::runtime_error("native speed is invalid");
        }
        for (unsigned i = 0; i < 3; ++i) {
            velocity[i] = direction[i] * speed;
        }
        if (actor.falls && actor.ground_distance > 0.0001f) {
            velocity[1] -= 0.2f;
        }
        if (!std::ranges::all_of(velocity, [](float x) { return std::isfinite(x); })) {
            throw std::runtime_error("native movement intent is not finite");
        }
        source = impulse ? "knockback" : "native_movement";
    }
    return {
        {"entity",    entity                                              },
        {"velocity",  velocity                                            },
        {"source",    source                                              },
        {"units",     "game_units_per_frame"                              },
        {"semantics", "movement intent before collision and subsequent AI"}
    };
}

nlohmann::json Snapshot(const MONSTOR &actor, const CCharacter &character, EntityHandles::Handle handle) {
    return {
        {"entity",       std::to_string(handle)   },
        {"status",       "live"                   },
        {"hp",           actor.hp                 },
        {"max_hp",       actor.max_hp             },
        {"defense",      actor.defense            },
        {"reward_exp",   actor.exp                },
        {"reward_money", actor.money              },
        {"drop_item",    actor.drop_item          },
        {"active",       actor.state == 2         },
        {"slow_frames",  actor.slow_timer         },
        {"stop_frames",  actor.stop_timer         },
        {"visual_scale", ReadEnemyScale(character)}
    };
}
} // namespace

bool IsEntityWrite(std::string_view operation) {
    return operation == "combat.heal" || operation == "combat.damage" || operation == "combat.status_add" ||
           operation == "entities.set_stats" || operation == "entities.reset_stats" ||
           operation == "entities.scale_set" || operation == "entities.scale_reset" || operation == "physics.velocity_set";
}

bool ApplyEntityAndSave(CSaveData &save, CMonstorUnit *unit, const std::vector<GameCommand> &commands,
                        int money, std::string &error, CommitEffects *effects, GeoramaState *georama, const EquipmentContext *equipment, bool shops_writable, bool town_loadout, bool active_floor) {
    if (effects) {
        *effects = {};
    }
    LootTransaction loot;
    if (!loot.Stage(commands, shops_writable, error)) return false;
    const bool has_loot = std::ranges::any_of(commands, [](const auto &command) { return IsLootWrite(command.operation); });
    ShopTransaction shops;
    PlayerCombatTransaction player;
    if (!player.Stage(save, commands, error)) return false;
    const bool has_player = std::ranges::any_of(commands, [](const auto &command) { return command.operation == "player.invincible"; });
    const bool has_tuning = std::ranges::any_of(commands, [](const auto &command) { return PlayerTuning::IsWrite(command.operation); });
    std::optional<PlayerTuning> tuning;
    if (has_tuning) {
        tuning = OwnedPlayerTuning();
        if (!tuning->Stage(commands, Scripts().Epoch(), error)) return false;
    }
    if (!shops.Stage(commands, shops_writable, error)) {
        return false;
    }
    std::vector<GameCommand> filtered;
    const bool               has_shops = std::ranges::any_of(commands, [](const auto &command) { return IsShopWrite(command.operation); });
    if (has_loot || has_shops || has_tuning || has_player) {
        for (const auto &command : commands) {
            if (!IsLootWrite(command.operation) && !IsShopWrite(command.operation) && !PlayerTuning::IsWrite(command.operation) && command.operation != "player.invincible") {
                filtered.push_back(command);
            }
        }
    }
    const auto &batch = has_loot || has_shops || has_tuning || has_player ? filtered : commands;
    if (!std::ranges::any_of(batch, [](const auto &command) { return IsEntityWrite(command.operation); })) {
        if (!ApplySave(save, batch, money, error, effects, georama, equipment, town_loadout, active_floor)) {
            return false;
        }
        shops.Commit();
        loot.Commit();
        if (tuning) OwnedPlayerTuning().Swap(*tuning);
        player.Commit();
        return true;
    }
    if (effects) {
        *effects = {};
    }
    EntityTransaction entities;
    if (!entities.Stage(unit, batch, error)) {
        return false;
    }
    std::vector<GameCommand> save_commands;
    for (const auto &command : batch) {
        if (!IsEntityWrite(command.operation)) {
            save_commands.push_back(command);
        }
    }
    if (!ApplySave(save, save_commands, money, error, effects, georama, equipment, town_loadout, active_floor)) {
        return false;
    }
    entities.Commit();
    shops.Commit();
    loot.Commit();
    if (tuning) OwnedPlayerTuning().Swap(*tuning);
    player.Commit();
    return true;
}

std::string ValidateEntityWrite(std::string_view operation, const nlohmann::json &a) {
    if (operation == "physics.velocity_set") {
        if (!a.is_object() || a.size() != 2 || !a.contains("entity") || !a["entity"].is_string() ||
            !Parse(a["entity"].get_ref<const std::string &>()) || !a.contains("velocity") ||
            !Vector3(a["velocity"], 100000)) {
            return "provide a current entity handle and velocity [x,y,z], each finite within +/-100000";
        }
        return {};
    }
    if (operation == "entities.scale_set" || operation == "entities.scale_reset") {
        const bool reset = operation == "entities.scale_reset";
        if (!a.is_object() || a.size() != (reset ? 1u : 2u) || !a.contains("entity") ||
            !a["entity"].is_string() || !Parse(a["entity"].get_ref<const std::string &>())) {
            return "provide a current entity handle and required scale fields";
        }
        if (!reset && (!a.contains("scale") || !a["scale"].is_number() ||
                       !std::isfinite(a["scale"].get<double>()) || a["scale"].get<double>() < 0.5 ||
                       a["scale"].get<double>() > 3.0)) {
            return "scale must be a finite multiplier from 0.5 to 3.0";
        }
        return {};
    }
    if (operation == "entities.set_stats" || operation == "entities.reset_stats") {
        if (!a.is_object() || !a.contains("entity") || !a["entity"].is_string() ||
            !Parse(a["entity"].get_ref<const std::string &>())) {
            return "provide a current entity handle";
        }
        if (operation == "entities.reset_stats") {
            return a.size() == 1 ? "" : "reset_stats takes only entity";
        }
        if (a.size() < 2) {
            return "provide at least one enemy stat";
        }
        for (const auto &[key, value] : a.items()) {
            if (key == "entity") {
                continue;
            }
            const auto it = std::ranges::find(EnemyStatNames, key);
            if (it == EnemyStatNames.end() || !value.is_number_integer()) {
                return "unknown or noninteger enemy stat: " + key;
            }
            const auto   index = it - EnemyStatNames.begin();
            const double minimum = index < 2 ? 1 : index == 5 ? -1
                                                              : 0;
            const double maximum = index < 2 ? 1000000 : index == 4 ? 2147483647
                                                     : index == 5   ? 376
                                                                    : 32767;
            const double number = value.get<double>();
            if (number < minimum || number > maximum ||
                (index == 5 && number != -1 && (number < 81 || number == 131))) {
                return "enemy stat out of range: " + key;
            }
        }
        return {};
    }
    const bool heal = operation == "combat.heal" || operation == "combat.damage";
    if (!IsEntityWrite(operation) || !a.is_object() || a.size() != (heal ? 2u : 3u) ||
        !a.contains("entity") || !a["entity"].is_string() || !Parse(a["entity"].get_ref<const std::string &>())) {
        return "provide an entity handle and the required combat fields";
    }
    const auto key = heal ? "amount" : "duration_frames";
    if (!a.contains(key) || !a[key].is_number_integer() || a[key].get<double>() < 1 ||
        a[key].get<double>() > (heal ? 1000000 : 32767)) {
        return "combat amount or duration is out of range";
    }
    if (!heal && (!a.contains("status") || !a["status"].is_string() ||
                  (a["status"] != "slow" && a["status"] != "stop"))) {
        return "enemy status must be slow or stop";
    }
    return {};
}

bool EntityTransaction::Stage(CMonstorUnit *unit, const std::vector<GameCommand> &commands, std::string &error) {
    unit_ = nullptr;
    values_ = {};
    owners_.reset();
    scales_.reset();
    error.clear();
    if (commands.size() > 64) {
        error = "callback write budget exceeded";
        return false;
    }
    for (const auto &command : commands) {
        if (!IsEntityWrite(command.operation)) {
            continue;
        }
        error = ValidateEntityWrite(command.operation, command.arguments);
        if (!error.empty()) {
            return false;
        }
        if (!unit) {
            error = "combat writes require an active dungeon";
            return false;
        }
        const auto slot = TrackEntities(*unit).Resolve(Parse(command.arguments.at("entity").get_ref<const std::string &>()));
        if (!slot || !Live(unit->monster[*slot]) || unit->monster[*slot].state != 2) {
            error = "enemy is stale, hidden, dead or not actively participating";
            return false;
        }
        const auto &actor = unit->monster[*slot];
        auto       &value = values_[*slot];
        if (!value.touched) {
            value = {true, ReadEnemyStats(actor), actor.slow_timer, actor.stop_timer};
        }
        auto &hp = value.stats[0];
        auto &max_hp = value.stats[1];
        if (hp <= 0 || max_hp < hp) {
            error = "enemy is already dying or has invalid native health";
            return false;
        }
        if (command.operation == "physics.velocity_set") {
            if (command.owner.empty() || value.stop > 0 || !std::isfinite(actor.ground_distance)) {
                error = "velocity requires a runtime owner, unstopped enemy and finite ground distance";
                return false;
            }
            auto requested = command.arguments.at("velocity").get<std::array<double, 3>>();
            if (actor.falls && actor.ground_distance > 0.0001f) {
                requested[1] += 0.2;
            }
            const double speed = std::hypot(requested[0], requested[1], requested[2]);
            value.speed = static_cast<float>(speed);
            for (unsigned axis = 0; axis < 3; ++axis) {
                value.direction[axis] = speed == 0 ? 0 : static_cast<float>(requested[axis] / speed);
            }
            value.velocity_touched = true;
            continue;
        }
        if (command.operation == "entities.scale_set" || command.operation == "entities.scale_reset") {
            if (command.owner.empty()) {
                error = "scale writes require a runtime package owner";
                return false;
            }
            if (!scales_) {
                scales_ = OwnedEnemyScales();
            }
            if (!value.scale_touched) {
                value.scale = ReadEnemyScale(unit->chara[*slot][0]);
            }
            if (command.operation == "entities.scale_reset") {
                if (const auto it = scales_->find(unit); it != scales_->end()) {
                    value.scale_touched |= it->second[*slot].Release(command.owner, value.scale);
                    PruneOwnedEnemyScales(*scales_);
                }
                continue;
            }
            if (!scales_->contains(unit) && scales_->size() >= 8) {
                error = "owned enemy scale unit capacity exhausted";
                return false;
            }
            if (!(*scales_)[unit][*slot].Claim(command.owner, value.scale, command.arguments.at("scale").get<float>(), error)) {
                return false;
            }
            value.scale_touched = true;
            continue;
        }
        if (command.operation == "entities.set_stats" || command.operation == "entities.reset_stats") {
            if (command.owner.empty()) {
                error = "stat writes require a runtime package owner";
                return false;
            }
            if (!owners_) {
                owners_ = OwnedEnemyStats();
            }
            if (command.operation == "entities.reset_stats") {
                if (const auto it = owners_->find(unit); it != owners_->end()) {
                    ReleaseEnemyStats(it->second[*slot], value.stats, command.owner);
                    PruneOwnedEnemyStats(*owners_);
                }
                continue;
            }
            if (!owners_->contains(unit) && owners_->size() >= 8) {
                error = "owned enemy stat unit capacity exhausted";
                return false;
            }
            auto       &claims = (*owners_)[unit][*slot];
            auto        requested = value.stats;
            const auto &args = command.arguments;
            for (unsigned i = 0; i < requested.size(); ++i) {
                if (args.contains(EnemyStatNames[i])) {
                    requested[i] = args.at(EnemyStatNames[i]).get<int>();
                }
            }
            if (args.contains("max_hp") && !args.contains("hp")) {
                requested[0] = ScaleEnemyHealth(hp, max_hp, requested[1]);
            }
            if (requested[0] > requested[1]) {
                error = "requested enemy HP exceeds maximum";
                return false;
            }
            const bool rebase_health = claims[1].owner == command.owner && max_hp == claims[1].applied &&
                                       hp != claims[0].applied;
            const int  original_health = rebase_health ? ScaleEnemyHealth(hp, max_hp, claims[1].original) : hp;
            for (unsigned i = 0; i < requested.size(); ++i) {
                if ((args.contains(EnemyStatNames[i]) || (i == 0 && args.contains("max_hp"))) &&
                    !claims[i].Claim(command.owner, value.stats[i], requested[i], error)) {
                    error = std::string(EnemyStatNames[i]) + ": " + error;
                    return false;
                }
            }
            if (rebase_health && (args.contains("hp") || args.contains("max_hp"))) {
                claims[0].original = original_health;
            }
            value.stats = requested;
            continue;
        }
        if (command.operation == "combat.damage") {
            if (actor.invincible_timer > 0) {
                continue;
            }
            if (!CanQueueEntityReactions(unit)) {
                error = "pending native damage reaction capacity is exhausted";
                return false;
            }
            value.damage = std::min(hp, command.arguments.at("amount").get<int>());
            hp -= value.damage;
            value.reaction = hp == 0 ? MONSTER_DMG_KILLED : MONSTER_DMG_HIT;
            continue;
        }
        if (command.operation == "combat.heal") {
            if (max_hp < hp) {
                error = "enemy health exceeds its native maximum";
                return false;
            }
            hp += std::min(command.arguments.at("amount").get<int>(), max_hp - hp);
        } else {
            auto &timer = command.arguments.at("status") == "slow" ? value.slow : value.stop;
            timer = std::max(timer, command.arguments.at("duration_frames").get<int>());
        }
    }
    unit_ = unit;
    return true;
}

void EntityTransaction::Commit() noexcept {
    if (!unit_) {
        return;
    }
    if (owners_) {
        OwnedEnemyStats().swap(*owners_);
    }
    if (scales_) {
        OwnedEnemyScales().swap(*scales_);
    }
    for (unsigned i = 0; i < values_.size(); ++i) {
        if (!values_[i].touched) {
            continue;
        }
        if (owners_) {
            WriteEnemyStats(unit_->monster[i], values_[i].stats);
        } else {
            unit_->monster[i].hp = values_[i].stats[0];
        }
        unit_->monster[i].slow_timer = values_[i].slow;
        unit_->monster[i].stop_timer = values_[i].stop;
        if (values_[i].velocity_touched) {
            auto &actor = unit_->monster[i];
            std::copy(values_[i].direction.begin(), values_[i].direction.end(), actor.movement);
            actor.movement[3] = 1;
            actor.movement_speed = values_[i].speed;
            actor.knockback[0] = 0;
        }
        if (values_[i].scale_touched) {
            WriteEnemyScale(unit_->chara[i][0], values_[i].scale);
        }
        if (values_[i].reaction != MONSTER_DMG_NONE) {
            auto &actor = unit_->monster[i];
            actor.last_attacker = -1;
            actor.last_hit_damage = values_[i].damage;
            actor.last_hit_id = -1;
            actor.hit_element = WEAPON_ELEMENT_NONE;
            QueueEntityReaction(unit_, i, values_[i].reaction);
        }
    }
    unit_ = nullptr;
}

std::string ValidateEntityQuery(std::string_view operation, const nlohmann::json &a) {
    if (!a.is_object()) {
        return "entity arguments must be an object";
    }
    if (operation == "entities.list") {
        return a.empty() ? "" : "entities.list takes no arguments";
    }
    if (operation == "physics.query") {
        if (a.size() != 2 || !a.contains("center") || !Vector3(a["center"], 1000000) ||
            !a.contains("radius") || !a["radius"].is_number() ||
            !std::isfinite(a["radius"].get<double>()) || a["radius"].get<double>() < 0 ||
            a["radius"].get<double>() > 10000) {
            return "provide center [x,y,z] within +/-1000000 and a finite radius from 0 to 10000";
        }
        return {};
    }
    if (operation != "entities.get" && operation != "physics.velocity_get") {
        return "unknown entity query";
    }
    if (a.size() != 1 || !a.contains("entity") || !a["entity"].is_string() ||
        !Parse(a["entity"].get_ref<const std::string &>())) {
        return "entity must be a canonical nonzero decimal handle string";
    }
    return {};
}

nlohmann::json ReadEntities(CMonstorUnit *unit, std::string_view operation, const nlohmann::json &a) {
    const auto error = ValidateEntityQuery(operation, a);
    if (!error.empty()) {
        throw std::runtime_error(error);
    }
    if (!unit) {
        StopTrackingEntities();
        if (operation == "entities.get") {
            return {
                {"entity", a.at("entity")},
                {"status", "expired"     }
            };
        }
        throw std::runtime_error("entity queries require an active dungeon");
    }
    auto &handles = TrackEntities(*unit);
    if (operation == "physics.query") {
        const auto   center = a.at("center").get<std::array<double, 3>>();
        const double radius = a.at("radius").get<double>();
        auto         matches = nlohmann::json::array();
        for (unsigned slot = 0; slot < EntityHandles::Capacity; ++slot) {
            if (!Live(unit->monster[slot]) || unit->monster[slot].state != 2 || !handles.Get(slot)) {
                continue;
            }
            float position[4]{};
            unit->chara[slot][0].GetPosition(position);
            const double distance = std::hypot(position[0] - center[0], position[1] - center[1], position[2] - center[2]);
            if (std::isfinite(distance) && distance <= radius) {
                matches.push_back({
                    {"entity",   std::to_string(handles.Get(slot))      },
                    {"distance", distance                               },
                    {"position", {position[0], position[1], position[2]}}
                });
            }
        }
        return {
            {"entities",  matches                                                       },
            {"semantics", "active revealed enemy origins in a sphere; no collision test"}
        };
    }
    if (operation == "entities.get" || operation == "physics.velocity_get") {
        const auto slot = handles.Resolve(Parse(a.at("entity").get_ref<const std::string &>()));
        if (!slot || !Live(unit->monster[*slot])) {
            if (operation == "physics.velocity_get") {
                throw std::runtime_error("enemy handle is expired");
            }
            return {
                {"entity", a.at("entity")},
                {"status", "expired"     }
            };
        }
        if (operation == "physics.velocity_get") {
            return Velocity(unit->monster[*slot], a.at("entity"));
        }
        return Snapshot(unit->monster[*slot], unit->chara[*slot][0], handles.Get(*slot));
    }
    auto result = nlohmann::json::array();
    for (unsigned slot = 0; slot < EntityHandles::Capacity; ++slot) {
        if (Live(unit->monster[slot]) && handles.Get(slot)) {
            result.push_back(Snapshot(unit->monster[slot], unit->chara[slot][0], handles.Get(slot)));
        }
    }
    return {
        {"entities", result}
    };
}

nlohmann::json ReadCombat(CMonstorUnit *unit, const CUserStatus &status) {
    if (!unit || status.cur_georama < 0 || status.cur_georama >= 6 || status.cur_floor < 0 || status.cur_floor >= 100) {
        throw std::runtime_error("combat snapshot requires a valid active dungeon floor");
    }
    return {
        {"dungeon",        status.cur_georama                                },
        {"floor",          status.cur_floor                                  },
        {"alive_count",    unit->alive_count                                 },
        {"recorded_kills", status.kills[status.cur_georama][status.cur_floor]}
    };
}
} // namespace mods
