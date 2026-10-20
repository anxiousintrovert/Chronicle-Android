#include "actor_runtime.hpp"
#include "actor_spawn.hpp"
#include "actor_cleanup.hpp"
#include "entity_tracking.hpp"
#include "character.hpp"
#include "dun/gameloop.hpp"
#include "dungeonmap.hpp"
#include "monstorunit.hpp"
#include "main.hpp"
#include "savedata.hpp"
#include <charconv>
#include <cmath>
extern s32 mode, gameTask, NextMapNo, existFlag;
extern int basic_se_table_no, se_table_no, now_voice_set, load_snd_set, load_voice_set;
namespace mods {
namespace {
using Json = nlohmann::json;
EntityHandles::Handle Handle(const Json &a) {
    if (!a.is_string()) return 0;
    const auto &text = a.get_ref<const std::string &>();
    if (text.empty() || text.size() > 20 || text[0] == '0') return 0;
    EntityHandles::Handle value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() ? value : 0;
}
std::string Unavailable() {
    if (mode != GAME_MODE_DUNGEON || gameTask != GAME_TASK_PLAY || BtGameModeFlag != BT_GAME_MODE_PLAY ||
        BtEventMode || NextMapNo >= 0 || existFlag || !BtAllDrawFlag)
        return "actors require stable dungeon play outside events and transitions";
    if (!SaveData || UserStatus != static_cast<CUserStatus *>(SaveData->GetDngStatus()) ||
        !NowMonstorUnit || !NowDngMap || !CharaMain.frame || selectMapNo < 0 || selectMapNo >= 7 ||
        selectMapNo != UserStatus->cur_georama)
        return "native actor scene is unavailable";
    if (load_snd_set >= 0 || load_voice_set >= 0) return "native sound loading is pending";
    return {};
}
std::array<float, 3> PlayerPosition() {
    float point[4]{}; CharaMain.GetPosition(point); return {point[0], point[1], point[2]};
}
std::string CheckSpawn(const Json &a) {
    auto error = Unavailable(); if (!error.empty()) return error;
    if (HasBoundActorOwnership(ActorOwnershipUnit()) && ActorOwnershipEpoch() != Scripts().Epoch())
        return "previous runtime actors are awaiting cleanup";
    const int model = a.at("model").get<int>();
    const auto position = a.at("position").get<std::array<float, 3>>();
    const auto plan = PlanLoadedActorSpawn(NowMonstorUnit, model, position, error);
    if (!plan) return error;
    if (UserStatus->cur_georama == 3 && UserStatus->cur_floor == 17 && plan->slot == 1)
        return "actor slot is reserved for native boss initialization";
    if (NowMonstorUnit->model[model].max_hp <= 0 || NowMonstorUnit->alive_count < 0 || NowMonstorUnit->alive_count >= NowMonstorUnit->SlotLimit())
        return "invalid live monster accounting or model health";
    const float radius = NowMonstorUnit->model[model].collision_radius;
    if (!ValidateActorPlacement(*NowDngMap, LoadedActorPlacementAreas(selectMapNo), position, radius, error) ||
        !ValidateActorObstructions(*NowDngMap, *NowMonstorUnit, position, radius, PlayerPosition(), 20, error)) return error;
    return {};
}
ActorRetentionResult &CleanupResult() { static ActorRetentionResult result; return result; }
}
std::string ValidateActorOperation(std::string_view operation, const Json &a) {
    if (!a.is_object()) return "actor arguments must be an object";
    if (operation == "entities.owned") return a.empty() ? "" : "entities.owned takes no arguments";
    if (operation == "entities.despawn")
        return a.size() == 1 && a.contains("entity") && Handle(a["entity"]) ? "" : "provide a canonical entity handle string";
    const bool spawn = operation == "entities.spawn";
    if (!spawn && operation != "entities.spawn_check") return "unknown actor operation";
    if (a.size() != (spawn ? 3 : 2) || !a.contains("model") || !a["model"].is_number_integer() ||
        a["model"].get<double>() < 0 || a["model"].get<double>() > 8 || !a.contains("position") ||
        !a["position"].is_array() || a["position"].size() != 3) return "provide model 0..8 and position [x,y,z]";
    for (const auto &axis : a["position"])
        if (!axis.is_number() || !std::isfinite(axis.get<double>()) || std::abs(axis.get<double>()) > 1000000)
            return "actor position must be finite and bounded";
    if (spawn) {
        if (!a.contains("id") || !a["id"].is_string()) return "provide a local actor id";
        const auto &id = a["id"].get_ref<const std::string &>();
        if (id.empty() || id.size() > 64) return "actor id must contain 1..64 ASCII identifier characters";
        for (const char c : id) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == ':')) return "invalid actor id character";
    }
    return {};
}
Json ReadActorOperation(std::string_view operation, const Json &a) {
    const auto error = ValidateActorOperation(operation, a); if (!error.empty()) throw std::runtime_error(error);
    if (operation == "entities.spawn_check") {
        const auto reason = CheckSpawn(a); return {{"ready", reason.empty()}, {"reason", reason}};
    }
    if (operation != "entities.owned") throw std::runtime_error("actor operation is not a query");
    Json actors = Json::array();
    auto *unit = ActorOwnershipUnit();
    if (HasBoundActorOwnership(unit)) {
        const auto *handles = TrackedEntities(unit);
        const auto &owners = BindActorOwnership(*unit, ActorOwnershipEpoch());
        for (const auto &[handle, record] : owners.Snapshot()) {
            std::string reason;
            for (const auto &failure : CleanupResult().pending) if (failure.entity == handle) reason = failure.reason;
            actors.push_back({{"entity", std::to_string(handle)}, {"owner", record.owner}, {"id", record.definition},
                {"current", unit == NowMonstorUnit && mode == GAME_MODE_DUNGEON && handles && handles->Resolve(handle).has_value()},
                {"cleanup_reason", reason}});
        }
    }
    return {{"actors", std::move(actors)}};
}
bool ApplyActorCommands(const std::vector<GameCommand> &commands, int money, std::string &error) {
    if (commands.size() != 1 || money || commands[0].owner.empty()) { error = "actor creation/retirement must be the sole owned engine write"; return false; }
    const auto &command = commands[0];
    if (command.operation != "entities.spawn" && command.operation != "entities.despawn") { error = "unknown actor write"; return false; }
    error = ValidateActorOperation(command.operation, command.arguments); if (!error.empty()) return false;
    error = Unavailable(); if (!error.empty()) return false;
    if (HasBoundActorOwnership(ActorOwnershipUnit()) && ActorOwnershipEpoch() != Scripts().Epoch()) {
        error = "previous runtime actors are awaiting cleanup"; return false;
    }
    if (command.operation == "entities.spawn") {
        error = CheckSpawn(command.arguments); if (!error.empty()) return false;
        TrackEntities(*NowMonstorUnit);
        auto &owners = BindActorOwnership(*NowMonstorUnit, Scripts().Epoch());
        const auto &id = command.arguments.at("id").get_ref<const std::string &>();
        for (const auto &[handle, record] : owners.Snapshot()) if (record.owner == command.owner && record.definition == id) {
            error = "actor id is already owned by this package"; return false;
        }
        return CreateLoadedActor(*NowMonstorUnit, *NowDngMap, LoadedActorPlacementAreas(selectMapNo), owners,
            command.owner, id, command.arguments.at("model").get<int>(), command.arguments.at("position").get<std::array<float, 3>>(),
            PlayerPosition(), 20, error).has_value();
    }
    if (!HasBoundActorOwnership(NowMonstorUnit)) { error = "no owned actors on this floor"; return false; }
    auto &owners = BindActorOwnership(*NowMonstorUnit, Scripts().Epoch());
    return RetireOwnedActor(NowMonstorUnit, owners, Handle(command.arguments.at("entity")), command.owner,
        NowColData, NowShotEffect, basic_se_table_no, se_table_no, now_voice_set, error);
}
void RetainGameActors() {
    auto *unit = ActorOwnershipUnit();
    if (!HasBoundActorOwnership(unit) || unit != NowMonstorUnit || !Unavailable().empty()) return;
    auto &owners = BindActorOwnership(*unit, ActorOwnershipEpoch());
    const auto active = ActorOwnershipEpoch() == Scripts().Epoch() ? Scripts().ActiveOwners() : std::vector<std::string>{};
    CleanupResult() = RetainOwnedActors(unit, owners, active, NowColData, NowShotEffect,
                                      basic_se_table_no, se_table_no, now_voice_set);
}
}
