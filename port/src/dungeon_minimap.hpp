#pragma once

#include <vector>

#include "rect.hpp"

class CDungeonMap;

enum class DungeonMiniMapKind {
    Tile,
    Player,
    Trap,
    Chest,
    Atla
};

struct DungeonMiniMapSprite {
    DungeonMiniMapKind kind;
    CRect_i_           screen;
    CRect_i_           atlas;
    unsigned char      tone;
    unsigned char      alpha;
};

// Expanded maps only. Retail DrawMiniMap retains its original arithmetic.
std::vector<DungeonMiniMapSprite> ExpandedDungeonMiniMap(
    const CDungeonMap &map, const float *position, int status, bool reveal_map, bool reveal_items);
