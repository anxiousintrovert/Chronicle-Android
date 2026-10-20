#pragma once
#include <optional>

#include "runtime.hpp"

namespace mods {
struct BackFloorContext {
    const void *save = nullptr;
    const void *map = nullptr;
    int         dungeon = -1, floor = -1;
    bool        active = false;
    bool        boundary = false;
    bool        native_request = false;
    std::string unavailable;
};

// Waiting for a script boundary must never carry an intent into a later floor.
class BackFloorQueue {
public:
    static constexpr std::uint64_t lifetime_frames = 1800;
    bool                           Queue(const BackFloorContext &context, std::uint64_t epoch, std::uint64_t frame,
                                         const std::vector<GameCommand> &commands, int money, std::string &error);
    bool                           Step(const BackFloorContext &context, std::uint64_t epoch, std::uint64_t frame,
                                        std::span<const std::string> owners);

    bool Pending() const { return pending_.has_value(); }

    nlohmann::json State() const { return state_; }

private:
    struct Request {
        BackFloorContext from;
        std::uint64_t    epoch, frame;
        std::string      owner;
    };

    void                   Complete(std::string_view state, std::string_view reason);
    std::optional<Request> pending_;
    nlohmann::json         state_ = {
        {"state", "idle"}
    };
};

nlohmann::json ReadBackFloor();
bool           QueueBackFloor(const std::vector<GameCommand> &commands, int money, std::string &error);
void           PollBackFloor();
bool           BackFloorTransportAccess(int flag);
} // namespace mods
