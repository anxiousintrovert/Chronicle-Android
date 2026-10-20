#include "owned_actors.hpp"
#include "entity_tracking.hpp"
#include <stdexcept>

namespace mods {
std::optional<ActorOwners::Reservation> ActorOwners::Reserve(const EntityHandles &handles, std::string_view owner,
                                                          std::string_view definition, std::string &error) const {
    if (!handles.Active() || (registry_ && registry_ != &handles)) { error = "actor identity registry is unavailable"; return {}; }
    if (owner.empty() || owner.size() > 128 || definition.empty() || definition.size() > 128) {
        error = "actor ownership requires bounded owner and definition IDs"; return {};
    }
    Records prepared;
    prepared.emplace(0, Record{std::string(owner), std::string(definition)});
    error.clear(); return Reservation{prepared.extract(0), &handles};
}
bool ActorOwners::Publish(const EntityHandles &handles, EntityHandles::Handle handle, Reservation &&reservation) noexcept {
    if (reservation.registry != &handles || reservation.node.empty() || !handles.Resolve(handle) ||
        (registry_ && registry_ != &handles) || records_.contains(handle)) return false;
    Expire(handles);
    if (records_.size() >= EntityHandles::Capacity) return false;
    reservation.node.key() = handle;
    const auto inserted = records_.insert(std::move(reservation.node));
    if (!inserted.inserted) return false;
    registry_ = &handles; return true;
}
bool ActorOwners::Claim(const EntityHandles &handles, EntityHandles::Handle handle,
                        std::string_view owner, std::string_view definition, std::string &error) {
    if (registry_ && registry_ != &handles) { error = "actor ownership belongs to another identity registry"; return false; }
    if (owner.empty() || owner.size() > 128 || definition.empty() || definition.size() > 128) {
        error = "actor ownership requires bounded owner and definition IDs"; return false;
    }
    if (!handles.Resolve(handle)) { error = "actor handle is stale or unavailable"; return false; }
    if (const auto existing = records_.find(handle); existing != records_.end()) {
        error = "actor is already owned by " + existing->second.owner; return false;
    }
    Expire(handles);
    if (records_.size() >= EntityHandles::Capacity) { error = "owned actor capacity reached"; return false; }
    records_.emplace(handle, Record{std::string(owner), std::string(definition)});
    registry_ = &handles;
    error.clear(); return true;
}
const ActorOwners::Record *ActorOwners::Find(const EntityHandles &handles, EntityHandles::Handle handle) const {
    if (registry_ != &handles || !handles.Resolve(handle)) return nullptr;
    const auto it = records_.find(handle);
    return it == records_.end() ? nullptr : &it->second;
}
bool ActorOwners::CheckOwner(const EntityHandles &handles, EntityHandles::Handle handle,
                             std::string_view owner, std::string &error) const {
    const auto *record = Find(handles, handle);
    if (!record) { error = "actor is not a live mod-owned entity"; return false; }
    if (owner.empty() || record->owner != owner) { error = "actor is owned by " + record->owner; return false; }
    error.clear(); return true;
}
std::vector<EntityHandles::Handle> ActorOwners::ByOwner(const EntityHandles &handles, std::string_view owner) const {
    std::vector<EntityHandles::Handle> result;
    if (registry_ != &handles) return result;
    for (const auto &[handle, record] : records_)
        if (record.owner == owner && handles.Resolve(handle)) result.push_back(handle);
    return result;
}
void ActorOwners::Expire(const EntityHandles &handles) {
    if (registry_ && registry_ != &handles) return;
    std::erase_if(records_, [&](const auto &entry) { return !handles.Resolve(entry.first); });
}
}

namespace mods {
namespace {
// Native global unit constructors call clear notifications. Do not initialize
// an ownership container from those calls, before any mod has requested one.
constinit CMonstorUnit *bound_actor_unit = nullptr;
constinit std::uint64_t bound_actor_epoch = 0;
ActorOwners &BoundOwners() { static ActorOwners owners; return owners; }
}
ActorOwners &BindActorOwnership(CMonstorUnit &unit, std::uint64_t epoch) {
    if (!TrackedEntities(&unit)) throw std::runtime_error("actor ownership requires an already tracked unit");
    auto &owners = BoundOwners();
    if (bound_actor_unit != &unit && owners.Size())
        throw std::runtime_error("another unit still has owned actors awaiting cleanup");
    if (owners.Size() && epoch != bound_actor_epoch)
        throw std::runtime_error("previous runtime still has owned actors awaiting cleanup");
    bound_actor_unit = &unit; bound_actor_epoch = epoch;
    return owners;
}
CMonstorUnit *ActorOwnershipUnit() { return bound_actor_unit; }
std::uint64_t ActorOwnershipEpoch() { return bound_actor_epoch; }
bool HasBoundActorOwnership(const CMonstorUnit *unit) {
    return unit && bound_actor_unit == unit && BoundOwners().Size() != 0;
}
void ClearBoundActorOwnership(CMonstorUnit *unit) {
    if (!unit || bound_actor_unit != unit) return;
    BoundOwners().Clear(); bound_actor_unit = nullptr; bound_actor_epoch = 0;
}
void ForgetBoundActorOwnership(CMonstorUnit *unit, EntityHandles::Handle handle) {
    if (unit && bound_actor_unit == unit) BoundOwners().Forget(handle);
}
}
