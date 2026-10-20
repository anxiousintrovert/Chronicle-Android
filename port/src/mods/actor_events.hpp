#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
class CMonstorUnit;

namespace mods {
// Internal capture transport. Public subscriptions are not exposed until the
// native producers and runtime dispatcher are integrated.
enum class ActorEventKind { Spawned, Removed };
enum class ActorEventSource { Native, Owned, OwnerCleanup };
struct ActorEvent {
    ActorEventKind kind=ActorEventKind::Spawned;
    ActorEventSource source=ActorEventSource::Native;
    std::uint64_t entity=0, epoch=0;
    CMonstorUnit *unit=nullptr; // Identity for filtering only; never dereferenced by consumers.
    int dungeon=-1, floor=-1, model=-1;
    int hp=0, max_hp=0;
    std::array<float,3> position{};
    std::array<char,129> owner{}, definition{};
};

class ActorEventQueue {
public:
    static constexpr unsigned Capacity=64;
    void Enable(bool value) noexcept { enabled_=value;if(!value)Reset(); }
    bool Enabled() const noexcept { return enabled_; }
    void Reset() noexcept { size_=0;dropped_=0; }
    // Copy bounded labels before publishing, so failed capture never leaves a
    // partial record. Capture never allocates, executes callbacks or reads RNG.
    bool Push(ActorEvent record,std::string_view owner={},std::string_view definition={}) noexcept {
        if(!enabled_)return false;
        if(owner.size()>=record.owner.size()||definition.size()>=record.definition.size()||
           owner.find('\0')!=std::string_view::npos||definition.find('\0')!=std::string_view::npos) {
            Drop();return false;
        }
        if(size_==Capacity){Drop();return false;}
        record.owner.fill(0);record.definition.fill(0);
        for(unsigned i=0;i<owner.size();++i)record.owner[i]=owner[i];
        for(unsigned i=0;i<definition.size();++i)record.definition[i]=definition[i];
        records_[size_++]=record;return true;
    }
    std::span<const ActorEvent> Pending() const noexcept { return {records_.data(),size_}; }
    std::uint32_t Dropped() const noexcept { return dropped_; }
    void CancelUnit(const CMonstorUnit *unit) noexcept {
        unsigned kept=0;
        for(unsigned i=0;i<size_;++i)if(records_[i].unit!=unit)records_[kept++]=records_[i];
        size_=kept;
    }
private:
    void Drop() noexcept {if(dropped_!=UINT32_MAX)++dropped_;}
    bool enabled_=false;
    unsigned size_=0;
    std::uint32_t dropped_=0;
    std::array<ActorEvent,Capacity> records_{};
};
} // namespace mods
