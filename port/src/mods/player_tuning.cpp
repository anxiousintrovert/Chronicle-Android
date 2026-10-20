#include "player_tuning.hpp"

#include <algorithm>
#include <cmath>

namespace mods {
namespace {
constexpr std::array<std::string_view, PlayerTuning::Count> names{
    "run_speed", "attack_anim_speed", "recoil_scale", "gauge_refill", "thirst_drain",
    "durability_wear", "invincible_bonus_frames", "map_reveal", "monster_reveal"};

std::size_t Index(std::string_view name) {
    return std::ranges::find(names, name) - names.begin();
}

double Default(std::size_t field) { return field < PlayerTuning::InvincibleBonus ? 1 : 0; }
} // namespace

bool PlayerTuning::IsWrite(std::string_view op) {
    return op == "player.tuning_set" || op == "player.tuning_clear";
}

std::string PlayerTuning::Validate(std::string_view op, const nlohmann::json &args) {
    if (!IsWrite(op) || !args.is_object()) {
        return "expected a player tuning operation and an argument object";
    }
    if (op == "player.tuning_clear") {
        return args.empty() ? "" : "player.tuning_clear takes no arguments";
    }
    for (auto it = args.begin(); it != args.end(); ++it) {
        const auto field = Index(it.key());
        if (field == Count) {
            return "unsupported player tuning field: " + it.key();
        }
        if (field >= MapReveal) {
            if (!it->is_boolean()) {
                return it.key() + " must be a boolean";
            }
        } else if (field == InvincibleBonus) {
            if (!it->is_number_integer() || it->get<double>() < 0 || it->get<double>() > 120) {
                return "invincible_bonus_frames must be an integer from 0 to 120";
            }
        } else if (!it->is_number() || !std::isfinite(it->get<double>()) ||
                   it->get<double>() < 0.25 || it->get<double>() > 4) {
            return it.key() + " must be a finite multiplier from 0.25 to 4";
        }
    }
    return {};
}

void PlayerTuning::Clear(std::string_view owner) {
    for (std::size_t i = 0; i < Count; ++i) {
        if (owners_[i] == owner) {
            owners_[i].clear();
            values_[i] = Default(i);
        }
    }
}

void PlayerTuning::Refresh() {
    engaged_ = std::ranges::any_of(owners_, [](const auto &owner) { return !owner.empty(); });
}

bool PlayerTuning::Stage(std::span<const GameCommand> commands, std::uint64_t epoch, std::string &error) {
    if (!std::ranges::any_of(commands, [](const auto &command) { return IsWrite(command.operation); })) {
        return true;
    }
    PlayerTuning next = epoch == epoch_ ? *this : PlayerTuning{};
    next.epoch_ = epoch;
    for (const auto &command : commands) {
        if (!IsWrite(command.operation)) {
            continue;
        }
        error = Validate(command.operation, command.arguments);
        if (!error.empty()) {
            return false;
        }
        if (command.owner.empty()) {
            error = "player tuning requires a runtime package owner";
            return false;
        }
        next.Clear(command.owner);
        if (command.operation == "player.tuning_clear") {
            continue;
        }
        for (auto it = command.arguments.begin(); it != command.arguments.end(); ++it) {
            const auto field = Index(it.key());
            if (!next.owners_[field].empty()) {
                error = "player tuning " + it.key() + " is owned by " + next.owners_[field];
                return false;
            }
            next.owners_[field] = command.owner;
            next.values_[field] = field >= MapReveal ? (it->get<bool>() ? 1 : 0) : it->get<double>();
        }
    }
    next.Refresh();
    Swap(next);
    return true;
}

void PlayerTuning::Retain(std::span<const std::string> owners, std::uint64_t epoch) {
    if (!engaged_) {
        return;
    }
    for (std::size_t i = 0; i < Count; ++i) {
        if (!owners_[i].empty() && (epoch != epoch_ || std::ranges::find(owners, owners_[i]) == owners.end())) {
            owners_[i].clear();
            values_[i] = Default(i);
        }
    }
    epoch_ = epoch;
    Refresh();
}

void PlayerTuning::Swap(PlayerTuning &other) noexcept {
    values_.swap(other.values_);
    owners_.swap(other.owners_);
    std::swap(epoch_, other.epoch_);
    std::swap(engaged_, other.engaged_);
}

nlohmann::json PlayerTuning::State() const {
    auto result = nlohmann::json{
        {"active",    engaged_                },
        {"epoch",     epoch_                  },
        {"effective", nlohmann::json::object()},
        {"owners",    nlohmann::json::object()}
    };
    for (std::size_t i = 0; i < Count; ++i) {
        result["owners"][names[i]] = owners_[i];
        if (i >= MapReveal) {
            result["effective"][names[i]] = values_[i] != 0;
        } else if (i == InvincibleBonus) {
            result["effective"][names[i]] = static_cast<int>(values_[i]);
        } else {
            result["effective"][names[i]] = values_[i];
        }
    }
    return result;
}
} // namespace mods
