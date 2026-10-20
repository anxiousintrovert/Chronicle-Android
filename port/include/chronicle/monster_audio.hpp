#pragma once
#include "chronicle/monster_limits.hpp"
namespace chronicle::monsters {
inline constexpr unsigned RetailSoundSequences = 32;
inline constexpr unsigned SoundSequenceCapacity = RetailSoundSequences + StorageCapacity;
unsigned SoundSequenceLimit();
void PrepareSoundSequencesForMonsterSlots(unsigned slots);
// Floor-load boundary only. Retail-to-retail is a no-op; leaving expanded
// routing queues stops for its outstanding leases before restoring byte IDs.
void PrepareEffectVoicesForMonsterSlots(unsigned slots);
bool ExpandedEffectVoicesActive();
}
