#include <gtest/gtest.h>
#include <stdexcept>

#include "chronicle/monster_scripts.hpp"
#include "dataalloc.hpp"
#include "monstorunit.hpp"
#include "platform/arena.hpp"

extern CDataAlloc2<1> MonstorScriptBuffer[16];

TEST(ModMonsterScripts, RetailBindingLeavesExtraStorageAndAllocationsDormant) {
    auto unit = std::make_unique<CMonstorUnit>();
    const auto allocated = chronicle::monsters::AllocatedExtraScriptSlots();
    CDataAlloc2<1> sentinel;
    unit->script[255] = &sentinel;
    const auto used = MonstorScriptBuffer[0].used;
    chronicle::monsters::BindScripts(*unit);
    for (unsigned i = 0; i < 16; ++i) EXPECT_EQ(unit->script[i], &MonstorScriptBuffer[i]);
    EXPECT_EQ(unit->script[255], &sentinel);
    EXPECT_EQ(MonstorScriptBuffer[0].used, used);
    EXPECT_EQ(chronicle::monsters::AllocatedExtraScriptSlots(), allocated);
    EXPECT_EQ(unit->SlotLimit(), 16u);
    EXPECT_THROW(chronicle::monsters::BindScripts(*unit, 257), std::invalid_argument);
    EXPECT_THROW(chronicle::monsters::BindScripts(*unit, 15), std::invalid_argument);
    EXPECT_EQ(unit->script[255], &sentinel);
    EXPECT_EQ(chronicle::monsters::AllocatedExtraScriptSlots(), allocated);
}

TEST(ModMonsterScripts, ExpandedBindingIsAlignedDistinctAndReusableAfterClear) {
    auto unit = std::make_unique<CMonstorUnit>();
    chronicle::monsters::BindScripts(*unit, 256);
    EXPECT_EQ(chronicle::monsters::AllocatedExtraScriptSlots(), 240u);
    for (unsigned i = 16; i < 256; ++i) {
        auto *arena = unit->script[i];
        ASSERT_NE(arena, nullptr);
        ASSERT_NE(arena->base, nullptr);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(arena->base) % 64, 0u);
        EXPECT_EQ(arena->limit, 1875 * kArenaHeadroom);
        EXPECT_EQ(arena->used, 0);
        for (unsigned j = 16; j < i; ++j) EXPECT_NE(arena->base, unit->script[j]->base);
        *arena->Alloc(1) = static_cast<unsigned char>(i);
    }
    auto *last = unit->script[255]->base;
    ArenaClearAll();
    EXPECT_EQ(*last, 0);
    chronicle::monsters::BindScripts(*unit, 256);
    EXPECT_EQ(unit->script[255]->base, last);
    EXPECT_EQ(unit->script[255]->used, 0);
    EXPECT_EQ(unit->SlotLimit(), 16u);
}
