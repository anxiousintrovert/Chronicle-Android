#include <gtest/gtest.h>
#include <stdexcept>
#include "chronicle/monster_audio.hpp"
#include "snd.hpp"
#include "mods/retail_sound.hpp"

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

TEST(ModMonsterSequences, ActorRetirementMatchesSoundAndVoiceWithoutTouchingDormantStorage) {
    RestoreSequences restore;
    using namespace chronicle::monsters;
    PrepareSoundSequencesForMonsterSlots(16); SndSeSeqInit();
    std::string error;
    int effect = -1, other = -1;
    for (int id = 0; id < 2800 && other < 0; ++id) {
        if (!mods::ResolveRetailEffect(id, -1, -1, -1, error)) continue;
        if (effect < 0) effect = id; else other = id;
    }
    ASSERT_GE(effect, 0); ASSERT_GE(other, 0);
    ASSERT_EQ(SndSeSeqPlayStop(effect, 10, 6), 1);
    ASSERT_EQ(SndSeSeqPlayStop(effect, 10, 8), 1);
    ASSERT_EQ(SndSeSeqPlayStop(other, 10, 6), 1);
    se_seq[32] = {static_cast<short>(effect), 10, 0, 6};
    EXPECT_FALSE(mods::RetireActorSoundSequence(effect, SlotCapacity, -1, -1, -1, error));
    EXPECT_EQ(se_seq[0].se_no, effect);
    EXPECT_FALSE(mods::RetireActorSoundSequence(2800, 3, -1, -1, -1, error));
    EXPECT_EQ(se_seq[0].se_no, effect);
    ASSERT_TRUE(mods::RetireActorSoundSequence(effect, 3, -1, -1, -1, error)) << error;
    EXPECT_EQ(se_seq[0].se_no, -1);
    EXPECT_EQ(se_seq[1].se_no, effect); EXPECT_EQ(se_seq[2].se_no, other);
    EXPECT_EQ(se_seq[32].se_no, effect);
    ASSERT_TRUE(mods::RetireActorSoundSequence(-1, 3, -1, -1, -1, error));
}
TEST(ModMonsterSequences, ActorRetirementSupportsExpandedVoiceWithoutShrinkingLimits) {
    RestoreSequences restore;
    using namespace chronicle::monsters;
    PrepareSoundSequencesForMonsterSlots(256);
    std::string error; int effect = -1;
    for (int id = 0; id < 2800; ++id) if (mods::ResolveRetailEffect(id, -1, -1, -1, error)) { effect = id; break; }
    ASSERT_GE(effect, 0);
    ASSERT_EQ(SndSeSeqPlayStop(effect, 10, 510), 1);
    ASSERT_EQ(SndSeSeqPlayStop(effect, 10, 508), 1);
    ASSERT_TRUE(mods::RetireActorSoundSequence(effect, 255, -1, -1, -1, error)) << error;
    EXPECT_EQ(se_seq[0].se_no, -1); EXPECT_EQ(se_seq[1].se_no, effect);
    EXPECT_EQ(SoundSequenceLimit(), SoundSequenceCapacity);
}
