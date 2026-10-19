#pragma once

#include <optional>

#include "runtime.hpp"

class CUserStatus;

namespace mods {
std::string ValidatePartySwitch(std::string_view operation, const nlohmann::json &args);
std::string CheckPartyTarget(const CUserStatus &status, int character);

// A resource-changing request is separate from save/entity transactions. Publish it only
// after a successful callback, then revalidate and load after MGEndFrame.
class PartySwitchQueue {
public:
    using Preflight = std::function<std::string(int)>;
    bool Queue(const CUserStatus *status, std::string_view unavailable, std::uint64_t epoch,
               const std::vector<GameCommand> &commands, int money, const Preflight &preflight, std::string &error);
    void Finish(CUserStatus *status, std::string_view unavailable, std::uint64_t epoch,
                std::span<const std::string> owners, const Preflight &preflight,
                const std::function<void(int)> &load);

    bool Pending() const { return pending_.has_value(); }

    nlohmann::json State() const { return state_; }

private:
    struct Request {
        const CUserStatus *status;
        std::string        owner;
        std::uint64_t      epoch;
        int                from, target;
    };

    std::optional<Request> pending_;
    nlohmann::json         state_ = {
        {"state", "idle"}
    };
};

nlohmann::json ReadPartySwitchState();
bool           QueuePartySwitch(const std::vector<GameCommand> &commands, int money, std::string &error);
void           FinishPartySwitch();
} // namespace mods
