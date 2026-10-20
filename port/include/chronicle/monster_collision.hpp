#pragma once

#include <span>
#include "collision.hpp"

class CMonstorUnit;

namespace chronicle::monsters {
// Explicit expanded-unit path only. Includes terrain first, then other active
// actors in slot order. Returned storage lasts until the next call.
// Rejects unsupported geometry/budgets before invoking native geometry writers.
std::span<CCPoly> ExpandedActorCollision(CMonstorUnit &unit, std::span<const CCPoly> terrain, const CBoxVu0 &box);
}
