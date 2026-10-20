#include "owned_actors.hpp"

namespace mods {
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
