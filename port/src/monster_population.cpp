#include "chronicle/monster_population.hpp"

#include <algorithm>
#include <stdexcept>
#include "chronicle/monster_collision.hpp"
#include "chronicle/monster_scripts.hpp"
#include "monstorunit.hpp"
#include "collisiondata.hpp"
#include "dungeonmap.hpp"
#include "dun/gameloop.hpp"
#include "chronicle/monster_audio.hpp"
#include "sound.hpp"

namespace {
CMonstorUnit *expanded_unit = nullptr;
CDungeonMap *expanded_map = nullptr;
CCollisionData *expanded_hits = nullptr;
}

namespace chronicle::monsters {
void PrepareSlotsForFloor(CMonstorUnit &unit, CDungeonMap &map, unsigned slots, int back_floor) {
    if (slots < RetailSlots || slots > StorageCapacity) throw std::invalid_argument("invalid monster slot limit");
    const auto previous = unit.SlotLimit();
    if (slots == RetailSlots && previous == RetailSlots) return;
    ValidateExpandedCollisionResources(map, unit.collision, slots);
    // Allocate all requested scripts before changing the visible active limit.
    BindScripts(unit, slots);
    unit.port_slot_limit = std::max(previous, slots);
    // Clear both newly enabled and retiring slots, including script/effect state.
    unit.CleanViewMonstor(back_floor);
    unit.port_slot_limit = slots;
    unit.current_monster = 0;
    for (unsigned i = slots; i < previous; ++i) unit.script[i] = nullptr;
}

void PreparePopulationForFloor(CMonstorUnit &unit, CDungeonMap &map, CCollisionData &hits, unsigned slots, int back_floor) {
    ValidateExpandedCollisionResources(map, unit.collision, slots);
    if (expanded_unit && expanded_unit != &unit) ResetExpandedPopulation();
    PrepareSlotsForFloor(unit, map, slots, back_floor);
    hits.PrepareForMonsterSlots(slots);
    // Sequence stops must still resolve through the old logical voice leases.
    PrepareSoundSequencesForMonsterSlots(slots);
    PrepareEffectVoicesForMonsterSlots(slots);
    expanded_unit = slots > RetailSlots ? &unit : nullptr;
    expanded_map = slots > RetailSlots ? &map : nullptr;
    expanded_hits = slots > RetailSlots ? &hits : nullptr;
}

void PrepareLoadedPopulation() {
    if (!NowMonstorUnit || !NowDngMap || !NowColData) return;
    const auto count = NowDngMap->PopulationCount(15, -1);
    PreparePopulationForFloor(*NowMonstorUnit, *NowDngMap, *NowColData,
                              std::max(RetailSlots, unsigned(count)), BtUraDongeon);
}

void ResetExpandedPopulation() {
    if (!expanded_unit) return;
    PreparePopulationForFloor(*expanded_unit, *expanded_map, *expanded_hits, RetailSlots, 0);
}

void FinishExpandedPopulationRun() {
    if (!expanded_unit) return;
    ResetExpandedPopulation();
    // RunGame can exit before the next native sound tick. Submit the queued
    // sequence/lease stops while audio resources are still alive.
    CSnd.Step();
}
}
