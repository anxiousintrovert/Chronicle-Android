#pragma once
#include <optional>

#include "runtime.hpp"

namespace mods {
struct WorldTransitionContext {
    const void *save = nullptr;
    int         mode = -1, map = -1, character = -1;
    std::string unavailable;
};

std::string ValidateWorldTransition(std::string_view operation, const nlohmann::json &arguments);

class WorldTransitionQueue {
public:
    using Preflight = std::function<std::string(int)>;
    bool Queue(const WorldTransitionContext &context, std::uint64_t epoch,
               const std::vector<GameCommand> &commands, int money, const Preflight &preflight, std::string &error);
    void Finish(const WorldTransitionContext &context, std::uint64_t epoch, std::span<const std::string> owners,
                const Preflight &preflight, const std::function<void(int)> &dispatch);

    bool Pending() const { return pending_.has_value(); }

    nlohmann::json State() const { return state_; }

private:
    struct Request {
        WorldTransitionContext from;
        std::uint64_t          epoch;
        std::string            owner;
        int                    target;
    };

    std::optional<Request> pending_;
    nlohmann::json         state_ = {
        {"state", "idle"}
    };
};

nlohmann::json ReadWorldLocations();
nlohmann::json ReadWorldTransition();
bool           QueueWorldTransition(const std::vector<GameCommand> &commands, int money, std::string &error);
void           FinishWorldTransition();
} // namespace mods
