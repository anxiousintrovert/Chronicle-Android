#pragma once

class CDungeonEventMan;
class CDungeonMap;

// Validate the complete expanded placement before mutating any runtime slot.
void PrepareExpandedDungeonEvents(CDungeonEventMan &manager, const CDungeonMap &map);
void RequireExpandedMapEventSlot(const CDungeonMap &map);

// Retail delegates unchanged; expanded maps reject missing placement geometry.
int SearchMapPutArea(CDungeonMap &map, float *position);
