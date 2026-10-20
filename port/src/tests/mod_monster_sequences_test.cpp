#include <gtest/gtest.h>
#include <stdexcept>
#include "chronicle/monster_audio.hpp"
#include "snd.hpp"

extern SND_SE_SEQ se_seq[chronicle::monsters::SoundSequenceCapacity];
void SndSeSeqAllStop();

namespace {
struct RestoreSequences {
    ~RestoreSequences() {
        chronicle::monsters::PrepareSoundSequencesForMonsterSlots(16);
        SndSeSeqInit();
    }
};
constexpr int SilentEffect = 2800; // rejected by audio lookup, still a valid sequence identity
}

TEST(ModMonsterSequences, RetailLimitAndDormantSequenceStorageStayUnchanged) {
    RestoreSequences restore;
    using namespace chronicle::monsters;
    PrepareSoundSequencesForMonsterSlots(16);
    SndSeSeqInit();
    se_seq[32].se_no = 1234;
    se_seq[32].step = 99;
    for (int i = 0; i < 32; ++i) ASSERT_EQ(SndSeSeqPlayStop(SilentEffect, 10, i * 2), 1);
    EXPECT_EQ(SndSeSeqPlayStop(SilentEffect, 10, 64), 0);
    PrepareSoundSequencesForMonsterSlots(16);
    EXPECT_EQ(SoundSequenceLimit(), 32u);
    EXPECT_EQ(se_seq[0].se_no, SilentEffect);
    EXPECT_EQ(se_seq[32].se_no, 1234);
    EXPECT_EQ(se_seq[32].step, 99);
    EXPECT_THROW(PrepareSoundSequencesForMonsterSlots(257), std::invalid_argument);
    EXPECT_EQ(se_seq[0].se_no, SilentEffect);
    SndSeSeqAllStop();
    EXPECT_EQ(se_seq[32].se_no, 1234);
}

TEST(ModMonsterSequences, ExpandedSequencesRefreshExpireAndRetireAtFullCapacity) {
    RestoreSequences restore;
    using namespace chronicle::monsters;
    SndSeSeqInit();
    PrepareSoundSequencesForMonsterSlots(256);
    ASSERT_EQ(SoundSequenceLimit(), SoundSequenceCapacity);
    for (unsigned i = 0; i < SoundSequenceCapacity; ++i) {
        ASSERT_EQ(SndSeSeqPlayStop(SilentEffect, 10, int(i * 2)), 1);
    }
    EXPECT_EQ(SndSeSeqPlayStop(SilentEffect, 10, 999), 0);
    EXPECT_EQ(SndSeSeqPlayStop(SilentEffect, 12, 510), 1);
    EXPECT_EQ(se_seq[255].voice, 510);
    EXPECT_EQ(se_seq[255].length, 12);
    EXPECT_EQ(se_seq[255].step, 1);
    for (unsigned i = 0; i < SoundSequenceCapacity; ++i) se_seq[i].length = 0;
    SndStep();
    for (unsigned i = 0; i < SoundSequenceCapacity; ++i) EXPECT_EQ(se_seq[i].se_no, -1);
    for (unsigned i = 0; i < SoundSequenceCapacity; ++i) ASSERT_EQ(SndSeSeqPlayStop(SilentEffect, 20, int(i * 2)), 1);
    PrepareSoundSequencesForMonsterSlots(16);
    EXPECT_EQ(SoundSequenceLimit(), 32u);
    for (unsigned i = 0; i < SoundSequenceCapacity; ++i) EXPECT_EQ(se_seq[i].se_no, -1);
}
