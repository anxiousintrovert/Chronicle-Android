#pragma once
#include <string>
#include <string_view>

namespace mods {
// Value-only ownership record, copied into a transaction before claiming a live
// field. Native unit/slot lifetime is validated separately by the entity adapter.
struct OwnedStat {
    std::string owner;
    int         original = 0, applied = 0;

    bool Claim(std::string_view caller, int current, int requested, std::string &error) {
        if (caller.empty()) {
            error = "stat writes require a runtime package owner";
            return false;
        }
        if (!owner.empty() && owner != caller) {
            error = "enemy stat is already owned by " + owner;
            return false;
        }
        // If native code superseded our last value, its latest value becomes the
        // restoration baseline for this new explicit request.
        if (owner.empty() || current != applied) {
            original = current;
        }
        owner = caller;
        applied = requested;
        error.clear();
        return true;
    }

    int Release(std::string_view caller, int current) {
        if (owner.empty() || owner != caller) {
            return current;
        }
        const int restored = current == applied ? original : current;
        *this = {};
        return restored;
    }
};
} // namespace mods
