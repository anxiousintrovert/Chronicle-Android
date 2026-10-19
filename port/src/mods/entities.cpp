#include "entities.hpp"

#include <algorithm>
#include <charconv>
#include <stdexcept>

#include "api.hpp"
#include "entity_tracking.hpp"
#include "monstorunit.hpp"
#include "runtime.hpp"

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
    return operation == "combat.heal" || operation == "combat.status_add";
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
    const bool heal = operation == "combat.heal";
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
            value = {true, actor.hp, actor.slow_timer, actor.stop_timer};
        }
        if (command.operation == "combat.heal") {
            if (actor.max_hp < value.hp) {
                error = "enemy health exceeds its native maximum";
                return false;
            }
            value.hp += std::min(command.arguments.at("amount").get<int>(), actor.max_hp - value.hp);
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
    for (unsigned i = 0; i < values_.size(); ++i) {
        if (!values_[i].touched) {
            continue;
        }
        unit_->monster[i].hp = values_[i].hp;
        unit_->monster[i].slow_timer = values_[i].slow;
        unit_->monster[i].stop_timer = values_[i].stop;
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
} // namespace mods
