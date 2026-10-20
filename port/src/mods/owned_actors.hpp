#pragma once
#include "entity_handles.hpp"
#include <map>
#include <string>
#include <string_view>
#include <vector>

class CMonstorUnit;
namespace mods {
// Metadata only: never creates, removes or changes a native actor. A caller must
// complete native cleanup before Forget; loss of an owner alone is not cleanup.
class ActorOwners {
public:
    struct Record { std::string owner, definition; };
    using Records = std::map<EntityHandles::Handle, Record>;
    struct Reservation { Records::node_type node; const EntityHandles *registry; };
    // Allocate metadata before native creation. Publish only attaches the node
    // to the resulting live generation and does not allocate.
    std::optional<Reservation> Reserve(const EntityHandles &handles, std::string_view owner,
                                       std::string_view definition, std::string &error) const;
    bool Publish(const EntityHandles &handles, EntityHandles::Handle handle, Reservation &&reservation) noexcept;
    bool Claim(const EntityHandles &handles, EntityHandles::Handle handle,
               std::string_view owner, std::string_view definition, std::string &error);
    const Record *Find(const EntityHandles &handles, EntityHandles::Handle handle) const;
    bool CheckOwner(const EntityHandles &handles, EntityHandles::Handle handle,
                    std::string_view owner, std::string &error) const;
    // Enumerate without erasing: the engine still needs these records to retire
    // live actors safely, without kill credit or dangling references.
    std::vector<EntityHandles::Handle> ByOwner(const EntityHandles &handles, std::string_view owner) const;
    void Forget(EntityHandles::Handle handle) { records_.erase(handle); }
    void Expire(const EntityHandles &handles);
    void Clear() { records_.clear(); registry_ = nullptr; }
    std::size_t Size() const { return records_.size(); }
    // Detached metadata for cleanup. Includes unresolved generations: losing a
    // handle alone does not prove that the corresponding native actor is gone.
    Records Snapshot() const { return records_; }
private:
    const EntityHandles *registry_ = nullptr;
    Records records_;
};
// Game-thread ownership binding. Empty/unbound state is dormant. Native lifetime
// notifications release records before invalidating their public identities.
ActorOwners &BindActorOwnership(CMonstorUnit &unit, std::uint64_t epoch = 0);
CMonstorUnit *ActorOwnershipUnit();
std::uint64_t ActorOwnershipEpoch();
bool HasBoundActorOwnership(const CMonstorUnit *unit);
void ClearBoundActorOwnership(CMonstorUnit *unit);
void ForgetBoundActorOwnership(CMonstorUnit *unit, EntityHandles::Handle handle);
}
