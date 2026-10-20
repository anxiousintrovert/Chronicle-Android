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
    using Preflight = std::function<std::string(int, int)>;
    bool Queue(const WorldTransitionContext &context, std::uint64_t epoch,
               const std::vector<GameCommand> &commands, int money, const Preflight &preflight, std::string &error);
    void Finish(const WorldTransitionContext &context, std::uint64_t epoch, std::span<const std::string> owners,
                const Preflight &preflight, const std::function<void(int, int, std::string_view)> &dispatch);

    bool Pending() const { return pending_.has_value(); }

    nlohmann::json State() const { return state_; }

private:
    struct Request {
        WorldTransitionContext from;
        std::uint64_t          epoch;
        std::string            owner;
        int                    target, floor;
    };

    std::optional<Request> pending_;
    nlohmann::json         state_ = {
        {"state", "idle"}
    };
};

// A floor intent crosses one native scene initialization and one entrance call.
class DungeonEntryRequest {
public:
    void Set(const void *save, std::uint64_t epoch, std::string owner, int dungeon, int floor);

    void Reset() { request_.reset(); }

    bool Pending() const { return request_.has_value(); }

    void Prepare(const void *save, std::uint64_t epoch, int dungeon, std::span<const std::string> owners);
    int  Take(const void *save, std::uint64_t epoch, int dungeon, std::span<const std::string> owners);

private:
    struct Request {
        const void   *save;
        std::uint64_t epoch;
        std::string   owner;
        int           dungeon, floor;
        bool          prepared = false;
    };

    bool                   Matches(const void *save, std::uint64_t epoch, int dungeon, std::span<const std::string> owners) const;
    std::optional<Request> request_;
};

nlohmann::json ReadWorldLocations();
nlohmann::json ReadWorldTransition();
bool           QueueWorldTransition(const std::vector<GameCommand> &commands, int money, std::string &error);
void           FinishWorldTransition();
void           PrepareDungeonEntry();
int            TakeDungeonEntryFloor(int dungeon);
nlohmann::json ReadDungeonLocations();
} // namespace mods
