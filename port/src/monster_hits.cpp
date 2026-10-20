#include "collisiondata.hpp"
#include "chronicle/monster_hits.hpp"
#include <algorithm>
#include <stdexcept>

unsigned CCollisionData::HitLimit() const {
    using namespace chronicle::monsters;
    return port_hit_limit >= RetailHitRecords && port_hit_limit <= HitStorageCapacity
        ? port_hit_limit : RetailHitRecords;
}

void CCollisionData::PrepareForMonsterSlots(unsigned slots) {
    using namespace chronicle::monsters;
    if (slots < RetailSlots || slots > StorageCapacity) throw std::invalid_argument("invalid monster hit slot limit");
    const auto desired = slots == RetailSlots ? RetailHitRecords : RetailHitRecords + slots * HitRecordsPerSlot;
    const auto previous = HitLimit();
    if (desired == RetailHitRecords && previous == RetailHitRecords) return;
    std::fill_n(active, std::max(previous, desired), 0);
    now_hit = 0;
    port_hit_limit = desired;
}
