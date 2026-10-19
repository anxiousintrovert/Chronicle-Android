#pragma once

#include <optional>

#include "runtime.hpp"

namespace mods {
struct TownSelectionContext {
    // A new token is required whenever the native town body/scene is rebuilt.
    std::uint64_t scene = 0;
    int           joined = 0;
    std::string   unavailable;
};

// Internal coordinator. The native adapter owns resources and supplies a safe
// post-frame context; this class does not read or write native/save globals.
class TownSelection {
public:
    TownSelection() = default;
    TownSelection(const TownSelection &) = delete;
    TownSelection &operator=(const TownSelection &) = delete;
    using Preflight = std::function<std::string(int)>;
    using Loader = std::function<std::string(int)>;

    bool Queue(const TownSelectionContext &context, std::uint64_t epoch,
               const std::vector<GameCommand> &commands, int money,
               const Preflight &preflight, std::string &error);
    void Finish(const TownSelectionContext &context, std::uint64_t epoch,
                std::span<const std::string> owners, const Preflight &preflight, const Loader &load);
    // Call only after the old scene's native body is retired and its motion
    // binding removed. Does not attempt to restore into a different scene.
    void SceneRetired();

    bool Engaged() const { return pending_.has_value() || lease_.has_value(); }

    nlohmann::json State() const;

private:
    struct Lease {
        std::string   owner;
        std::uint64_t scene, epoch;
        int           character = 0;
        bool          restore = false;
    };

    struct Request {
        std::string   owner;
        std::uint64_t scene, epoch;
        int           character;
    };

    std::optional<Lease>   lease_;
    std::optional<Request> pending_;
    std::string            state_ = "idle", reason_;
};
} // namespace mods
