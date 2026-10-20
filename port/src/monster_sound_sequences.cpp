#include "chronicle/monster_audio.hpp"
#include "snd.hpp"
#include <stdexcept>

void SndSeSeqAllStop();

namespace chronicle::monsters {
namespace {
unsigned sequence_limit = RetailSoundSequences;
}
unsigned SoundSequenceLimit() { return sequence_limit; }

void PrepareSoundSequencesForMonsterSlots(unsigned slots) {
    if (slots < RetailSlots || slots > StorageCapacity) throw std::invalid_argument("invalid monster sequence slot limit");
    const auto desired = slots == RetailSlots ? RetailSoundSequences : RetailSoundSequences + slots;
    if (desired == RetailSoundSequences && sequence_limit == RetailSoundSequences) return;
    // Stop through the old limit before shrinking; initialize every new slot
    // before lookup is allowed to see it.
    SndSeSeqAllStop();
    sequence_limit = desired;
    SndSeSeqInit();
}
}
