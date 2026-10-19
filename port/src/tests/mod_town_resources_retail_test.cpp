#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "character.hpp"
#include "dataalloc.hpp"
#include "dataread.hpp"
#include "dataread_port.hpp"
#include "dataset.hpp"
#include "editloop.hpp"
#include "gameloop.hpp"
#include "gfx_fixture.hpp"
#include "mods/town_motion.hpp"
#include "platform/clock.hpp"
#include "platform/config.hpp"
#include "platform/input.hpp"
#include "platform/paths.hpp"
#include "savedata.hpp"

namespace {
TEST(ModTownRetail, RepeatedBodiesAndMappedMotionsUseBoundedSeparateStorage) {
    const char *data = std::getenv("DC_DATA");
    const char *audit = std::getenv("DC_TOWN_RESOURCE_AUDIT");
    if (!data || !audit || !*audit) {
        GTEST_SKIP() << "Run alone with DC_DATA and DC_TOWN_RESOURCE_AUDIT (a disposable output directory)";
    }
    namespace fs = std::filesystem;
    const fs::path root = audit;
    fs::create_directories(root);
    PathsSetDataRoot(data);
    PathsSetSaveRoot(root);
    ConfigLoad();
    dc::test::GfxFixture graphics(640, 480, 1.0f, dc::test::GfxOptions{.offscreen = true});
    InputInit();
    ClockSetUnbounded(true);
    ClockReset();
    ASSERT_TRUE(GameSetJump("edit:0"));
    GameSetFastLoad(true);
    GameSetAutosave(false);
    GameSetFrameBudget(90);
    const int result = RunGame(0, nullptr);
    InputShutdown();
    ASSERT_EQ(result, 0);
    ASSERT_NE(SaveData, nullptr);
    ASSERT_EQ(Chara, static_cast<CCharacter *>(&MainChara));
    ASSERT_NE(MainChara.frame, nullptr);

    // The game has finished its last frame. Resource replacement below never
    // happens inside native rendering or in a gameplay callback.
    const auto           status_before = *SaveData->GetDngStatus();
    const auto           native_base = CharaBuffer.base;
    const int            native_limit = CharaBuffer.limit;
    const int            native_used = CharaBuffer.used;
    std::array<float, 4> position{}, rotation{};
    MainChara.GetPosition(position.data());
    MainChara.GetRotation(rotation.data());
    static CDataAlloc2<1> replacement_arena(-1);
    SetDataBuffer(&replacement_arena, 115000);
    ASSERT_NE(replacement_arena.base, CharaBuffer.base);

    constexpr std::array packs{
        "chara/c01d.chr", "dun/mainchara/c04b.chr", "dun/mainchara/c06b.chr",
        "dun/mainchara/c05a.chr", "dun/mainchara/c10b.chr", "dun/mainchara/c18a.chr"};
    std::array<int, 6> used{};
    nlohmann::json     report = nlohmann::json::array();
    for (int cycle = 0; cycle < 3; ++cycle) {
        for (int member = 1; member <= 5; ++member) {
            SCOPED_TRACE("cycle=" + std::to_string(cycle) + " member=" + std::to_string(member));
            ASSERT_TRUE(DataFileAvailable(packs[member]));
            replacement_arena.Reset();
            EdLoadMainChara(const_cast<char *>(packs[member]), const_cast<char *>("base.cfg"), &replacement_arena);
            ASSERT_NE(MainChara.frame, nullptr);
            ASSERT_NE(MainChara.shadow_frame, nullptr);
            MainChara.SetPosition(position.data());
            MainChara.SetRotation(rotation.data());
            mods::TownMotionBinding binding(MainChara, member);
            for (int town_motion = 0; town_motion <= 10; ++town_motion) {
                SCOPED_TRACE("motion=" + std::to_string(town_motion));
                int         index = -1, set = -1;
                const auto *motion = MainChara.GetMotionParam(town_motion, &index, nullptr, nullptr, &set);
                ASSERT_NE(motion, nullptr);
                ASSERT_NE(motion->motion_info, nullptr);
                ASSERT_GE(index, 0);
                ASSERT_GE(set, 0);
                ASSERT_NE(MainChara.shadow_motion[set], nullptr);
                ASSERT_NE(MainChara.shadow_motion[set]->motion_info, nullptr);
                std::fprintf(stderr, "town resources: cycle=%d member=%d town_motion=%d index=%d set=%d used=%d\n",
                             cycle, member, town_motion, index, set, replacement_arena.used);
                MainChara.SetMotion(town_motion, 0);
                MainChara.Step();
                MainChara.ShadowStep();
            }
            EXPECT_GT(replacement_arena.used, 0);
            EXPECT_LT(replacement_arena.used, replacement_arena.limit);
            if (cycle == 0) {
                used[member] = replacement_arena.used;
            }
            EXPECT_EQ(replacement_arena.used, used[member]);
            EXPECT_EQ(CharaBuffer.base, native_base);
            EXPECT_EQ(CharaBuffer.limit, native_limit);
            EXPECT_EQ(CharaBuffer.used, native_used);
            EXPECT_EQ(std::memcmp(&status_before, SaveData->GetDngStatus(), sizeof(status_before)), 0);
            report.push_back({
                {"cycle",  cycle                 },
                {"member", member                },
                {"quads",  replacement_arena.used}
            });
        }
    }
    // Restore the retail body before leaving the graphics fixture. No binding
    // survives to remap Toan, and the native arena retains its original size.
    CharaBuffer.Reset();
    EdLoadMainChara(const_cast<char *>(packs[0]), const_cast<char *>("info.cfg"), &CharaBuffer);
    MainChara.SetPosition(position.data());
    MainChara.SetRotation(rotation.data());
    EXPECT_EQ(CharaBuffer.base, native_base);
    EXPECT_EQ(CharaBuffer.limit, native_limit);
    EXPECT_EQ(CharaBuffer.used, native_used);
    EXPECT_EQ(std::memcmp(&status_before, SaveData->GetDngStatus(), sizeof(status_before)), 0);
    std::ofstream(root / "resources.json") << report.dump(2);
}
} // namespace
