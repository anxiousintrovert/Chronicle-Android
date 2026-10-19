#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>

#include "character.hpp"
#include "main.hpp"
#include "mods/town_motion.hpp"

extern s32 mode;

namespace {
class ModTownMotion : public testing::Test {
protected:
    CCharacter character{};
    CCharacter other{};
    s32        previous_mode = mode;

    void SetUp() override { mode = GAME_MODE_EDIT; }

    void TearDown() override { mode = previous_mode; }
};

TEST_F(ModTownMotion, DormantPathIsIdentityForEveryModeAndMotion) {
    for (int scene : std::array<int, 3>{GAME_MODE_EDIT, GAME_MODE_DUNGEON, -1}) {
        mode = scene;
        for (int motion : {-1, 0, 5, 10, 11, 34, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
            EXPECT_EQ(mods::ResolveTownMotion(&character, motion), motion);
            EXPECT_EQ(mods::ResolveTownMotion(nullptr, motion), motion);
        }
    }
}

TEST_F(ModTownMotion, ScopedBindingMapsOnlySelectedCharacterInTown) {
    constexpr std::array expected{0, 1, 2, 0, 0, 34, 35, 31, 32, 0, 0};
    for (int member = 1; member <= 5; ++member) {
        {
            mods::TownMotionBinding binding(character, member);
            for (int motion = 0; motion < static_cast<int>(expected.size()); ++motion) {
                EXPECT_EQ(mods::ResolveTownMotion(&character, motion), expected[motion]);
                EXPECT_EQ(mods::ResolveTownMotion(&other, motion), motion);
            }
            EXPECT_EQ(mods::ResolveTownMotion(&character, -1), -1);
            EXPECT_EQ(mods::ResolveTownMotion(&character, 34), 34);
            mode = GAME_MODE_DUNGEON;
            EXPECT_EQ(mods::ResolveTownMotion(&character, 5), 5);
            mode = GAME_MODE_EDIT;
        }
        EXPECT_EQ(mods::ResolveTownMotion(&character, 5), 5);
    }
}

TEST_F(ModTownMotion, InvalidOrCompetingBindingsDoNotReplaceLiveSelection) {
    EXPECT_THROW(mods::TownMotionBinding invalid(character, 0), std::invalid_argument);
    EXPECT_THROW(mods::TownMotionBinding invalid(character, 6), std::invalid_argument);
    EXPECT_EQ(mods::ResolveTownMotion(&character, 5), 5);
    mods::TownMotionBinding binding(character, 1);
    EXPECT_THROW(mods::TownMotionBinding conflicting(other, 2), std::logic_error);
    EXPECT_EQ(mods::ResolveTownMotion(&character, 5), 34);
    EXPECT_EQ(mods::ResolveTownMotion(&other, 5), 5);
}

TEST_F(ModTownMotion, NativeLookupReturnsMappedSetWithoutMutatingCharacterOrRng) {
    // Use separate sets so the test checks index/range/set output, not only the number.
    tagMOTION_TYPE town{}, dungeon{};
    character.motion[0] = &town;
    character.motion_start[0] = 0;
    character.motion_end[0] = 11;
    character.motion[1] = &dungeon;
    character.motion_start[1] = 31;
    character.motion_end[1] = 36;
    std::array<unsigned char, sizeof(CCharacter)> before;
    std::memcpy(before.data(), static_cast<const void *>(&character), before.size());
    srand(719);
    const int next = rand();
    srand(719);
    int index = -1, start = -1, end = -1, set = -1;
    EXPECT_EQ(character.GetMotionParam(5, &index, &start, &end, &set), &town);
    EXPECT_EQ(index, 5);
    {
        mods::TownMotionBinding binding(character, 1);
        EXPECT_EQ(character.GetMotionParam(5, &index, &start, &end, &set), &dungeon);
        EXPECT_EQ(index, 3);
        EXPECT_EQ(start, 31);
        EXPECT_EQ(end, 36);
        EXPECT_EQ(set, 1);
        EXPECT_EQ(character.GetMotionParam(-1, &index, nullptr, nullptr, nullptr), nullptr);
    }
    EXPECT_EQ(character.GetMotionParam(5, &index, &start, &end, &set), &town);
    EXPECT_EQ(std::memcmp(before.data(), static_cast<const void *>(&character), before.size()), 0);
    EXPECT_EQ(rand(), next);
}

TEST_F(ModTownMotion, NativeShadowUsesTheSameMappedSetAndIndexAsTheModel) {
    tagMOTION_TYPE              town{}, dungeon{}, town_shadow{}, dungeon_shadow{};
    std::array<MOTION_INFO, 11> town_info{}, town_shadow_info{};
    std::array<MOTION_INFO, 5>  dungeon_info{}, dungeon_shadow_info{};
    town.motion_info = town_info.data();
    dungeon.motion_info = dungeon_info.data();
    town_shadow.motion_info = town_shadow_info.data();
    dungeon_shadow.motion_info = dungeon_shadow_info.data();
    character.motion[0] = &town;
    character.shadow_motion[0] = &town_shadow;
    character.motion_start[0] = 0;
    character.motion_end[0] = 11;
    character.motion[1] = &dungeon;
    character.shadow_motion[1] = &dungeon_shadow;
    character.motion_start[1] = 31;
    character.motion_end[1] = 36;
    character.motion_no = 5;
    character.motion_flags = 1;
    // Native ShadowStep applies hold flags before the null-frame exit. This
    // exercises its independent lookup without submitting a fabricated rig.
    character.shadow_frame = nullptr;
    town_shadow_info[5].speed = dungeon_shadow_info[3].speed = 2.0f;
    {
        mods::TownMotionBinding binding(character, 1);
        character.ShadowStep();
        EXPECT_EQ(dungeon_shadow_info[3].speed, 0.0f);
        EXPECT_EQ(town_shadow_info[5].speed, 2.0f);
    }
    dungeon_shadow_info[3].speed = 2.0f;
    character.ShadowStep();
    EXPECT_EQ(town_shadow_info[5].speed, 0.0f);
    EXPECT_EQ(dungeon_shadow_info[3].speed, 2.0f);
    EXPECT_EQ(character.motion_no, 5);
}
} // namespace
