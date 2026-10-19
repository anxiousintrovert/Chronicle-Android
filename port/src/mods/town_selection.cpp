#include "town_selection.hpp"

#include <algorithm>

namespace mods {
namespace {
std::string Ready(const TownSelectionContext &context, int character) {
    if (!context.scene) {
        return "town scene is unavailable";
    }
    if (!context.unavailable.empty()) {
        return context.unavailable;
    }
    if (context.joined < 1 || context.joined > 6) {
        return "invalid native party size";
    }
    if (character < 0 || character >= context.joined) {
        return "character has not joined the party";
    }
    return {};
}

std::string Probe(const TownSelection::Preflight &preflight, int character) {
    try {
        return preflight ? preflight(character) : std::string{};
    } catch (const std::exception &e) {
        return std::string("town resource preflight failed: ") + e.what();
    } catch (...) {
        return "town resource preflight failed";
    }
}

std::string Load(const TownSelection::Loader &load, int character) {
    try {
        return load ? load(character) : "town character loader unavailable";
    } catch (const std::exception &e) {
        return std::string("town character load failed: ") + e.what();
    } catch (...) {
        return "town character load failed";
    }
}
} // namespace

bool TownSelection::Queue(const TownSelectionContext &context, std::uint64_t epoch,
                          const std::vector<GameCommand> &commands, int money,
                          const Preflight &preflight, std::string &error) {
    if (commands.size() != 1 || money || commands[0].operation != "party.town_set_active") {
        error = "town selection must be the callback's only engine write";
        return false;
    }
    const auto &command = commands[0];
    const auto &args = command.arguments;
    if (!args.is_object() || args.size() != 1 || !args.contains("character") ||
        !args["character"].is_number_integer() || args["character"].get<double>() < 0 ||
        args["character"].get<double>() > 5) {
        error = "provide only character, an integer from 0 to 5";
        return false;
    }
    if (command.owner.empty()) {
        error = "town selection requires a runtime package owner";
        return false;
    }
    if (pending_) {
        error = "a town selection request is already pending for " + pending_->owner;
        return false;
    }
    if (lease_ && (lease_->owner != command.owner || lease_->epoch != epoch ||
                   lease_->scene != context.scene || lease_->restore)) {
        error = "town character is owned by " + lease_->owner + " or awaiting restoration";
        return false;
    }
    const int target = args["character"];
    error = Ready(context, target);
    if (error.empty() && target != (lease_ ? lease_->character : 0)) {
        error = Probe(preflight, target);
    }
    if (!error.empty()) {
        return false;
    }
    pending_ = Request{command.owner, context.scene, epoch, target};
    state_ = "pending";
    reason_.clear();
    return true;
}

void TownSelection::Finish(const TownSelectionContext &context, std::uint64_t epoch,
                           std::span<const std::string> owners, const Preflight &preflight, const Loader &load) {
    if (!Engaged()) {
        return;
    }
    const auto alive = [&](const auto &request) {
        return request.epoch == epoch && std::ranges::find(owners, request.owner) != owners.end();
    };
    // Scene mismatch alone is not proof that old native pointers were retired.
    // Keep the lease until the adapter explicitly acknowledges retirement.
    if ((lease_ && lease_->scene != context.scene) || (pending_ && pending_->scene != context.scene)) {
        pending_.reset();
        state_ = "awaiting_scene_retirement";
        reason_ = "town scene changed; native resource retirement must be acknowledged";
        return;
    }
    if (lease_ && (!alive(*lease_) || lease_->character >= context.joined)) {
        lease_->restore = true;
    }
    if (lease_ && lease_->restore) {
        pending_.reset();
        state_ = "restoring";
        reason_ = Ready(context, 0);
        if (reason_.empty()) {
            reason_ = Probe(preflight, 0);
        }
        if (reason_.empty()) {
            reason_ = Load(load, 0);
        }
        if (reason_.empty()) {
            lease_.reset();
            state_ = "restored";
        }
        return;
    }
    if (!pending_) {
        return;
    }
    const auto request = std::move(*pending_);
    pending_.reset();
    if (!alive(request)) {
        state_ = "cancelled";
        reason_ = "request owner stopped or runtime restarted";
        return;
    }
    reason_ = Ready(context, request.character);
    const int current = lease_ ? lease_->character : 0;
    if (reason_.empty() && request.character != current) {
        reason_ = Probe(preflight, request.character);
    }
    if (!reason_.empty()) {
        state_ = "failed";
        return;
    }
    if (request.character != current) {
        // Retain a cleanup obligation even if a native loader partially changes
        // resources and then fails. Do not hand the body to a competing owner.
        if (!lease_) {
            lease_ = Lease{request.owner, request.scene, request.epoch};
        }
        reason_ = Load(load, request.character);
        if (!reason_.empty()) {
            lease_->restore = true;
            state_ = "failed";
            return;
        }
        lease_->character = request.character;
    }
    if (!request.character) {
        lease_.reset();
    }
    state_ = "complete";
}

void TownSelection::SceneRetired() {
    if (!Engaged()) {
        return;
    }
    pending_.reset();
    lease_.reset();
    state_ = "retired";
    reason_ = "native town scene retired";
}

nlohmann::json TownSelection::State() const {
    return {
        {"state",           state_                             },
        {"reason",          reason_                            },
        {"owner",           lease_ ? lease_->owner : ""        },
        {"character",       lease_ ? lease_->character : 0     },
        {"restore_pending", lease_ && lease_->restore          },
        {"pending_owner",   pending_ ? pending_->owner : ""    },
        {"target",          pending_ ? pending_->character : -1}
    };
}
} // namespace mods
