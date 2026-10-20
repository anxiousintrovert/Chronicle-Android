#include "chronicle/monster_population.hpp"

#include <algorithm>
#include <stdexcept>
#include "chronicle/monster_collision.hpp"
#include "chronicle/monster_scripts.hpp"
#include "monstorunit.hpp"

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
}
