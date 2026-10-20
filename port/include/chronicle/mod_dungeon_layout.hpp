#pragma once
class CDungeonMap;

namespace mods {
// Called at the native floor-generation boundary. False preserves retail generation.
bool BuildOwnedDungeonLayout();
void RecordOwnedPopulationFailure(CDungeonMap *map, int requested, const char *reason);
void RecordOwnedPopulation(CDungeonMap *map, int requested, int placed);
}
