#pragma once

#include "chronicle/monster_limits.hpp"

class CMonstorUnit;

namespace chronicle::monsters {
// Loading/transition boundary only: extra arenas are shared by the active unit,
// just like the native sixteen script buffers. This does not activate slots.
void BindScripts(CMonstorUnit &unit);
void BindScripts(CMonstorUnit &unit, unsigned slots);
unsigned AllocatedExtraScriptSlots();
}
