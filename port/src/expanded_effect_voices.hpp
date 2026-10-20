#pragma once
#include <array>
#include <cstdint>

namespace chronicle_audio {
struct EffectIdentity {
    int port, bank, program, voice;
    bool operator==(const EffectIdentity &) const = default;
};

// Logical IDs remain full-width; only leased tokens enter the byte protocol.
// The mixer has 48 physical voices, so 256 logical leases leave ample room.
class ExpandedEffectVoices {
    struct Lease {
        EffectIdentity identity{};
        std::uint64_t age = 0;
        bool active = false;
    };
    std::array<Lease, 256> leases{};
    std::uint64_t clock = 0;
public:
    int Find(EffectIdentity identity) const {
        for (unsigned i = 0; i < leases.size(); ++i) {
            if (leases[i].active && leases[i].identity == identity) return int(i);
        }
        return -1;
    }
    template<class Stop> int Play(EffectIdentity identity, Stop stop) {
        if (const auto token = Find(identity); token >= 0) {
            leases[token].age = ++clock;
            return token;
        }
        unsigned token = 0;
        for (unsigned i = 0; i < leases.size(); ++i) {
            if (!leases[i].active) { token = i; break; }
            if (leases[i].age < leases[token].age) token = i;
        }
        if (leases[token].active) stop(leases[token].identity, token);
        leases[token] = {identity, ++clock, true};
        return int(token);
    }
    void Forget(EffectIdentity identity) {
        if (const auto token = Find(identity); token >= 0) leases[token].active = false;
    }
    template<class Stop> void Clear(Stop stop) {
        for (unsigned i = 0; i < leases.size(); ++i) {
            if (leases[i].active) stop(leases[i].identity, i);
            leases[i].active = false;
        }
    }
};
}
