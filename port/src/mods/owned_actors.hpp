#pragma once
#include "entity_handles.hpp"
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace mods {
// Metadata only: never creates, removes or changes a native actor. A caller must
// complete native cleanup before Forget; loss of an owner alone is not cleanup.
class ActorOwners {
public:
    struct Record { std::string owner, definition; };
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
private:
    const EntityHandles *registry_ = nullptr;
    std::map<EntityHandles::Handle, Record> records_;
};
}
