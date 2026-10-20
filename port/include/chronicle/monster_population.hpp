#pragma once

class CMonstorUnit;
class CDungeonMap;

namespace chronicle::monsters {
// Explicit floor-loading boundary only, after collision resources load and
// before placing actors. Preflight failure preserves the previous active unit.
// A retail-to-retail request is a no-op.
void PrepareSlotsForFloor(CMonstorUnit &unit, CDungeonMap &map, unsigned slots, int back_floor);
}
