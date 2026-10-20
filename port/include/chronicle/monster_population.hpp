#pragma once
#include <string>

class CMonstorUnit;
class CDungeonMap;
class CCollisionData;

namespace chronicle::monsters {
// Explicit floor-loading boundary only, after collision resources load and
// before placing actors. Preflight failure preserves the previous active unit.
// A retail-to-retail request is a no-op.
void PrepareSlotsForFloor(CMonstorUnit &unit, CDungeonMap &map, unsigned slots, int back_floor);
void PreparePopulationForFloor(CMonstorUnit &unit, CDungeonMap &map, CCollisionData &hits, unsigned slots, int back_floor);
// Returns a preflight rejection reason after restoring native limits; empty on success.
std::string PrepareRequestedPopulation(CMonstorUnit &unit, CDungeonMap &map, CCollisionData &hits, unsigned slots, int back_floor);
void PrepareLoadedPopulation();
void ResetExpandedPopulation();
void FinishExpandedPopulationRun();
}
