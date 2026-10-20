#include "actor_spawn.hpp"
#include "monstorunit.hpp"
#include "dataalloc.hpp"
#include "entity_tracking.hpp"
#include "model_identity.hpp"
#include "userstatus.hpp"
#include "dun/gameloop.hpp"
#include <stdexcept>
#include <cmath>
#include <iterator>

namespace mods {
std::optional<LoadedActorSpawnPlan> PlanLoadedActorSpawn(const CMonstorUnit *unit, int model,
                                                       std::array<float, 3> position, std::string &error) {
    const auto fail = [&](const char *reason) -> std::optional<LoadedActorSpawnPlan> { error = reason; return {}; };
    if (!unit) return fail("dungeon monster unit unavailable");
    for (float value : position)
        if (!std::isfinite(value) || std::abs(value) > 1000000.0f) return fail("spawn position must be finite and bounded");
    if (unit->model_count < 0 || unit->model_count > static_cast<int>(std::size(unit->model)) ||
        model < 0 || model >= unit->model_count) return fail("monster model is not loaded on this floor");
    const auto &definition = unit->model[model];
    if (definition.attachment_kind == 8 || definition.kind == MONSTER_KIND_MIMIC_SMALL || definition.kind == MONSTER_KIND_MIMIC_LARGE)
        return fail("mimic spawning requires a separate chest-trigger lifecycle");
    if (!unit->script_data[model]) return fail("loaded monster script unavailable");
    if (!unit->base_chara[model][0].frame) return fail("loaded monster model frame unavailable");
    for (int part = 1; part < 4; ++part)
        if (definition.model_name[part][0] && !unit->base_chara[model][part].frame)
            return fail("loaded monster attachment frame unavailable");
    int slot = -1;
    for (int i = 0; i < unit->SlotLimit(); ++i) if (unit->monster[i].state == -1) { slot = i; break; }
    if (slot < 0) return fail("no free slot within the active monster limit");
    // SetupViewMonstor chooses the first free slot too. Never skip an unready
    // first slot and accidentally validate a different arena than native uses.
    const auto *arena = unit->script[slot];
    // SetupViewMonstor resets the arena; BtSetEventScript reserves 64 + 384 quadwords.
    if (!arena || !arena->base || arena->limit < 448)
        return fail("first free monster slot has insufficient script storage");
    error.clear(); return LoadedActorSpawnPlan{slot, model, position};
}
}

namespace mods {
std::optional<EntityHandles::Handle> CreateLoadedActor(
    CMonstorUnit &unit, const CDungeonMap &map, std::span<const ITEM_FREE_AREA> areas,
    ActorOwners &owners, std::string_view owner, std::string_view definition,
    int model, std::array<float, 3> position, std::array<float, 3> player_position,
    float player_radius, std::string &error, const ActorSpawnTuning &tuning) {
    const auto fail = [&](const char *reason) -> std::optional<EntityHandles::Handle> { error = reason; return {}; };
    error = ValidateActorSpawnTuning(tuning); if (!error.empty()) return {};
    auto *handles = TrackedEntities(&unit);
    if (!handles) return fail("actor identity tracking is unavailable");
    const auto plan = PlanLoadedActorSpawn(&unit, model, position, error);
    if (!plan) return {};
    if (!handles->CanConstruct(plan->slot)) return fail("actor identity generations exhausted");
    if (!UserStatus || NowMonstorUnit != &unit || NowDngMap != &map)
        return fail("native dungeon context does not match actor creation");
    // This native slot invokes boss-specific frame initialization. Generic
    // spawning must not enter that special encounter path.
    if (UserStatus->cur_georama == 3 && UserStatus->cur_floor == 17 && plan->slot == 1)
        return fail("actor slot is reserved for native boss initialization");
    if (unit.alive_count < 0 || unit.alive_count >= unit.SlotLimit() || unit.model[model].max_hp <= 0)
        return fail("invalid live monster accounting or model health");
    const float radius = unit.model[model].collision_radius;
    if (!ValidateActorPlacement(map, areas, position, radius, error) ||
        !ValidateActorObstructions(map, unit, position, radius, player_position, player_radius, error)) return {};
    auto reservation = owners.Reserve(*handles, owner, definition, error);
    if (!reservation) return {};
    // No mod callback is delivered inside native setup. Slot selection and the
    // construction notification therefore use the preflighted slot/registry.
    if (!unit.SetupViewMonstor(model, position.data(), -1)) return fail("native monster creation rejected");
    const auto handle = handles->Get(plan->slot);
    if (!owners.Publish(*handles, handle, std::move(*reservation)))
        throw std::logic_error("native actor creation violated its ownership reservation");
    auto &actor = unit.monster[plan->slot];
    if (tuning.max_hp) {
        actor.max_hp = *tuning.max_hp;
        // Do not resurrect an actor whose native initialization ended its life.
        if ((actor.state == 1 || actor.state == 2) && actor.hp > 0) actor.hp = *tuning.max_hp;
    }
    if (tuning.defense) actor.defense = static_cast<short>(*tuning.defense);
    if (tuning.reward_exp) actor.exp = *tuning.reward_exp;
    if (tuning.reward_money) actor.money = *tuning.reward_money;
    if (tuning.rotation) unit.chara[plan->slot][0].SetRotation((*tuning.rotation)[0], (*tuning.rotation)[1], (*tuning.rotation)[2]);
    error.clear(); return handle;
}
}

namespace mods {
nlohmann::json ReadLoadedActorModels(const CMonstorUnit *unit) {
    using Json = nlohmann::json;
    Json result = {{"available", false}, {"reason", "dungeon monster unit unavailable"},
                   {"slot_limit", 0}, {"free_slots", 0}, {"models", Json::array()}};
    if (!unit) return result;
    if (unit->model_count < 0 || unit->model_count > static_cast<int>(std::size(unit->model))) {
        result["reason"] = "invalid loaded monster model count"; return result;
    }
    result["available"] = true; result["reason"] = ""; result["slot_limit"] = unit->SlotLimit();
    int free = 0;
    for (int slot = 0; slot < unit->SlotLimit(); ++slot) free += unit->monster[slot].state == -1;
    result["free_slots"] = free;
    for (int index = 0; index < unit->model_count; ++index) {
        const auto &model = unit->model[index];
        std::string reason;
        const auto plan = PlanLoadedActorSpawn(unit, index, {}, reason);
        const bool radius_valid = std::isfinite(model.collision_radius) && model.collision_radius >= 0 && model.collision_radius <= 1000;
        if (reason.empty() && (model.max_hp <= 0 || !radius_valid)) reason = "invalid loaded model health or radius";
        const int native_id = NativeMonsterModelId(unit, index);
        result["models"].push_back({{"native_model_id", native_id >= 0 ? Json(native_id) : Json(nullptr)}, {"model", index}, {"kind", model.kind}, {"name", model.name_no},
            {"max_hp", model.max_hp}, {"radius", radius_valid ? Json(model.collision_radius) : Json(nullptr)},
            {"resources_ready", plan.has_value() && reason.empty()}, {"reason", reason}});
    }
    return result;
}
}

namespace mods {
std::string ValidateActorSpawnTuning(const ActorSpawnTuning &tuning) {
    if (tuning.max_hp && (*tuning.max_hp < 1 || *tuning.max_hp > 1000000)) return "spawn max_hp must be 1..1000000";
    if (tuning.defense && (*tuning.defense < 0 || *tuning.defense > 32767)) return "spawn defense must be 0..32767";
    if (tuning.reward_exp && (*tuning.reward_exp < 0 || *tuning.reward_exp > 32767)) return "spawn reward_exp must be 0..32767";
    if (tuning.reward_money && *tuning.reward_money < 0) return "spawn reward_money must be 0..2147483647";
    if (tuning.rotation) for (const auto axis : *tuning.rotation)
        if (!std::isfinite(axis) || std::abs(axis) > 10000) return "spawn rotation must be finite radians within +/-10000";
    return {};
}
std::string ParseActorSpawnTuning(const nlohmann::json &arguments, ActorSpawnTuning &output) {
    if (!arguments.is_object()) return "actor arguments must be an object";
    ActorSpawnTuning tuning;
    if (arguments.contains("stats")) {
        const auto &stats = arguments.at("stats");
        if (!stats.is_object() || stats.empty()) return "spawn stats must be a nonempty object";
        for (const auto &[key, value] : stats.items()) {
            std::optional<int> *destination = nullptr;
            int minimum = 0, maximum = 32767;
            if (key == "max_hp") { destination = &tuning.max_hp; minimum = 1; maximum = 1000000; }
            else if (key == "defense") destination = &tuning.defense;
            else if (key == "reward_exp") destination = &tuning.reward_exp;
            else if (key == "reward_money") { destination = &tuning.reward_money; maximum = 2147483647; }
            else return "unsupported spawn stat: " + key;
            if (!value.is_number_integer() || value.get<double>() < minimum || value.get<double>() > maximum)
                return "spawn stat is not an integer within its allowed range: " + key;
            *destination = value.get<int>();
        }
    }
    if (arguments.contains("rotation")) {
        const auto &rotation = arguments.at("rotation");
        if (!rotation.is_array() || rotation.size() != 3) return "spawn rotation must be [x,y,z] radians";
        std::array<float, 3> values;
        for (unsigned i = 0; i < 3; ++i) {
            if (!rotation[i].is_number() || !std::isfinite(rotation[i].get<double>()) || std::abs(rotation[i].get<double>()) > 10000)
                return "spawn rotation must be finite radians within +/-10000";
            values[i] = rotation[i].get<float>();
        }
        tuning.rotation = values;
    }
    const auto error = ValidateActorSpawnTuning(tuning);
    if (!error.empty()) return error;
    output = tuning; return {};
}
}
