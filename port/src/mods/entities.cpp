#include "entities.hpp"

#include <algorithm>
#include <charconv>
#include <stdexcept>

#include "api.hpp"
#include "entity_reactions.hpp"
#include "entity_tracking.hpp"
#include "monstorunit.hpp"
#include "runtime.hpp"
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

nlohmann::json Snapshot(const MONSTOR &actor, EntityHandles::Handle handle) {
    return {
        {"entity",       std::to_string(handle)},
        {"status",       "live"                },
        {"hp",           actor.hp              },
        {"max_hp",       actor.max_hp          },
        {"defense",      actor.defense         },
        {"reward_exp",   actor.exp             },
        {"reward_money", actor.money           },
        {"drop_item",    actor.drop_item       },
        {"active",       actor.state == 2      },
        {"slow_frames",  actor.slow_timer      },
        {"stop_frames",  actor.stop_timer      }
    };
}
} // namespace

bool IsEntityWrite(std::string_view operation) {
    return operation == "combat.heal" || operation == "combat.damage" || operation == "combat.status_add" ||
           operation == "entities.set_stats" || operation == "entities.reset_stats";
}

bool ApplyEntityAndSave(CSaveData &save, CMonstorUnit *unit, const std::vector<GameCommand> &commands,
                        int money, std::string &error, CommitEffects *effects, GeoramaState *georama) {
    if (!std::ranges::any_of(commands, [](const auto &command) { return IsEntityWrite(command.operation); })) {
        return ApplySave(save, commands, money, error, effects, georama);
    }
    if (effects) {
        *effects = {};
    }
    EntityTransaction entities;
    if (!entities.Stage(unit, commands, error)) {
        return false;
    }
    std::vector<GameCommand> save_commands;
    for (const auto &command : commands) {
        if (!IsEntityWrite(command.operation)) {
            save_commands.push_back(command);
        }
    }
    if (!ApplySave(save, save_commands, money, error, effects, georama)) {
        return false;
    }
    entities.Commit();
    return true;
}

std::string ValidateEntityWrite(std::string_view operation, const nlohmann::json &a) {
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
    if (operation != "entities.get") {
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
    if (operation == "entities.get") {
        const auto slot = handles.Resolve(Parse(a.at("entity").get_ref<const std::string &>()));
        if (!slot || !Live(unit->monster[*slot])) {
            return {
                {"entity", a.at("entity")},
                {"status", "expired"     }
            };
        }
        return Snapshot(unit->monster[*slot], handles.Get(*slot));
    }
    auto result = nlohmann::json::array();
    for (unsigned slot = 0; slot < EntityHandles::Capacity; ++slot) {
        if (Live(unit->monster[slot]) && handles.Get(slot)) {
            result.push_back(Snapshot(unit->monster[slot], handles.Get(slot)));
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
