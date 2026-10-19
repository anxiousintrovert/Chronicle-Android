#include "entities.hpp"

#include <charconv>
#include <stdexcept>

#include "entity_tracking.hpp"
#include "monstorunit.hpp"

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
