#pragma once

#include <span>
#include "collision.hpp"

class CMonstorUnit;
class CDungeonMap;
class CFrame;

namespace chronicle::monsters {
// Read-only floor-load preflight, including worst-case chest/Atla and actor
// contributions. Retail slot requests return without inspecting resources.
void ValidateExpandedCollisionResources(CDungeonMap &map, CFrame *actor_collision, unsigned slots);
// Procedural-floor terrain, in native neighbor/chest/Atla order. Every writer's
// upper bound is checked before allocating and publishing scratch storage.
// Explicit expanded-unit caller only; storage lasts until the next terrain call.
std::span<CCPoly> ExpandedTerrainCollision(CDungeonMap &map, const float *position, const float *player_position);

// Explicit expanded-unit path only. Includes terrain first, then other active
// actors in slot order. Returned storage lasts until the next call.
// Rejects unsupported geometry/budgets before invoking native geometry writers.
std::span<CCPoly> ExpandedActorCollision(CMonstorUnit &unit, std::span<const CCPoly> terrain, const CBoxVu0 &box);
}
