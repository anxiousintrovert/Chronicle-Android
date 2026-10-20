#include "chronicle/monster_scripts.hpp"

#include <array>
#include <stdexcept>

#include "dataalloc.hpp"
#include "dataread.hpp"
#include "monstorunit.hpp"

extern CDataAlloc2<1> MonstorScriptBuffer[16];

namespace chronicle::monsters {
namespace {
unsigned allocated = 0;

auto &ExtraScripts() {
    // No extra descriptor construction or arena mapping on the retail path.
    static std::array<CDataAlloc2<1>, StorageCapacity - RetailSlots> extra;
    return extra;
}
}

unsigned AllocatedExtraScriptSlots() { return allocated; }

void BindScripts(CMonstorUnit &unit) { BindScripts(unit, unit.SlotLimit()); }

void BindScripts(CMonstorUnit &unit, unsigned slots) {
    if (slots < RetailSlots || slots > StorageCapacity) {
        throw std::invalid_argument("monster script slots exceed supported storage");
    }
    if (slots > RetailSlots) {
        auto &extra = ExtraScripts();
        const unsigned count = slots - RetailSlots;
        // Prepare every requested arena before publishing pointers to the unit.
        // SetDataBuffer uses the upstream guarded, aligned arena implementation.
        for (unsigned i = 0; i < count; ++i) {
            SetDataBuffer(&extra[i], 1875);
        }
        if (count > allocated) allocated = count;
        for (unsigned i = 0; i < count; ++i) {
            unit.script[RetailSlots + i] = &extra[i];
        }
    }
    for (unsigned i = 0; i < RetailSlots; ++i) {
        unit.script[i] = &MonstorScriptBuffer[i];
    }
}
}
