#pragma once
class CDungeonMap;

namespace mods {
// Called at the native floor-generation boundary. False preserves retail generation.
bool BuildOwnedDungeonLayout();
void RecordOwnedPopulation(CDungeonMap *map, int requested, int placed);
}
