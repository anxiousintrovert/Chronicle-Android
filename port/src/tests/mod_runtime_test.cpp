#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "mods/runtime.hpp"
#include "mods/save.hpp"
#include "mods/session.hpp"

namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;

class ModRuntime : public testing::Test {
protected:
    fs::path      root = fs::temp_directory_path() / ("chronicle-runtime-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    mods::Manager manager;
    mods::Runtime runtime;

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    void Write(const fs::path &path, const std::string &data) {
        fs::create_directories(path.parent_path());
        std::ofstream f(path, std::ios::binary);
        f << data;
        ASSERT_TRUE(f.good());
    }

    Json Manifest(const std::string &id = "test.mod") {
        return {
            {"schema_version", 1                                                                                                            },
            {"id",             id                                                                                                           },
            {"name",           id                                                                                                           },
            {"version",        "1"                                                                                                          },
            {"game",           "chronicle"                                                                                                  },
            {"files",          Json::object()                                                                                               },
            {"api_version",    1                                                                                                            },
            {"script",         "main.lua"                                                                                                   },
            {"quests",         {{{"id", "visit"}, {"title", "Visit Norune"}, {"description", "Return to Norune."}, {"goal", 2}, {"map", 0}}}}
        };
    }

    void Add(const std::string &code, Json manifest = nullptr) {
        if (manifest.is_null()) {
            manifest = Manifest();
        }
        const auto path = root / "mods" / manifest["id"].get<std::string>();
        Write(path / "mod.json", manifest.dump());
        Write(path / "main.lua", code);
    }

    void Start() {
        manager.Scan({root / "mods"}, {});
        runtime.Start(manager);
    }

    Json State() { return Json::parse(runtime.SaveState())["mods"]["test.mod"]; }
};

TEST_F(ModRuntime, LuaReceivesGameEventsAndIsolatedState) {
    Add("return {on_tick=function(w) chronicle.set('map', w.map); chronicle.set('ticks', (chronicle.get('ticks') or 0)+1) end}");
    Start();
    runtime.Tick({1, 0, 50, true});
    runtime.Tick({1, 0, 50, true});
    EXPECT_EQ(State()["values"]["ticks"], 2);
    EXPECT_EQ(State()["values"]["map"], 0);
    EXPECT_TRUE(runtime.Diagnostics().empty());
}

TEST_F(ModRuntime, QuestMapEntryDoesNotRepeatEveryTickOrOnMenus) {
    Add("return {}");
    Start();
    runtime.Tick({1, 0, 0, true});
    runtime.Tick({1, 0, 0, true});
    runtime.Tick({2, 0, 0, false});
    runtime.Tick({1, 0, 0, true});
    ASSERT_EQ(runtime.Quests().size(), 1u);
    EXPECT_EQ(runtime.Quests()[0].progress, 1);
    std::string error;
    ASSERT_TRUE(runtime.LoadState(runtime.SaveState(), error)) << error;
    runtime.Tick({1, 0, 0, true});
    EXPECT_EQ(runtime.Quests()[0].progress, 1);
    runtime.Tick({1, 1, 0, true});
    runtime.Tick({1, 0, 0, true});
    EXPECT_EQ(runtime.Quests()[0].progress, 2);
}

TEST_F(ModRuntime, ScriptsCanAdvanceDeclaredQuestAndRewardOnce) {
    auto m = Manifest();
    m["permissions"] = {"economy.write"};
    m["quests"][0].erase("map");
    Add("return {on_tick=function() if chronicle.quest_get('visit')<2 then chronicle.quest_add('visit',1) elseif not chronicle.get('paid') then assert(chronicle.add_money(25)); chronicle.set('paid',true) end end}", m);
    Start();
    int money = 0;
    for (int i = 0; i < 5; ++i) {
        runtime.Tick({1, 0, money, true}, [&](int n) {money+=n;return true; });
    }
    EXPECT_EQ(money, 25);
    EXPECT_EQ(runtime.Quests()[0].progress, 2);
    std::string error;
    const auto  state = runtime.SaveState();
    EXPECT_TRUE(runtime.LoadState(state, error)) << error;
    runtime.Tick({1, 0, money, true}, [&](int n) {money+=n;return true; });
    EXPECT_EQ(money, 25);
}

TEST_F(ModRuntime, SandboxHasNoFilesystemOrCodeLoading) {
    Add("assert(io==nil and os==nil and package==nil and debug==nil and load==nil and dofile==nil and pcall==nil); return {on_tick=function() chronicle.set('safe',true) end}");
    Start();
    runtime.Tick({1, 0, 0, true});
    EXPECT_TRUE(runtime.Diagnostics().empty());
    EXPECT_EQ(State()["values"]["safe"], true);
}

TEST_F(ModRuntime, CurrencyRequiresPermissionAndCallbackFailureRollsBack) {
    Add("return {on_tick=function() chronicle.set('changed',true); assert(chronicle.add_money(10)) end}");
    Start();
    int calls = 0;
    runtime.Tick({1, 0, 0, true}, [&](int) {++calls;return true; });
    EXPECT_EQ(calls, 0);
    EXPECT_FALSE(runtime.Diagnostics().empty());
    EXPECT_FALSE(State().contains("values"));
}

TEST_F(ModRuntime, InfiniteLoopStopsOnlyFaultingScript) {
    Add("return {on_tick=function() while true do end end}");
    Add("return {on_tick=function() chronicle.set('alive',true) end}", Manifest("test.healthy"));
    Start();
    runtime.Tick({1, 0, 0, true});
    EXPECT_FALSE(runtime.Diagnostics().empty());
    EXPECT_EQ(Json::parse(runtime.SaveState())["mods"]["test.healthy"]["values"]["alive"], true);
}

TEST_F(ModRuntime, MemoryBudgetContainsLargeAllocation) {
    Add("return {on_tick=function() local huge=string.rep('x',32*1024*1024) end}");
    Start();
    runtime.Tick({1, 0, 0, true});
    EXPECT_FALSE(runtime.Diagnostics().empty());
}

TEST_F(ModRuntime, MissingCapabilityBlocksPackageBeforeExecution) {
    auto m = Manifest();
    m["requires_capabilities"] = {"save.branch.v1"};
    Add("error('should not run')", m);
    Start();
    EXPECT_TRUE(manager.LoadOrder().empty());
    EXPECT_TRUE(runtime.Diagnostics().empty());
    EXPECT_NE(manager.Packages()[0].problems.front().find("save.branch.v1"), std::string::npos);
}

TEST_F(ModRuntime, ExclusiveGameplayClaimsBlockBothPackages) {
    auto m = Manifest();
    m["exclusive"] = {"combat.scaling"};
    Add("return {}", m);
    m["id"] = "test.other";
    Add("return {}", m);
    Start();
    EXPECT_TRUE(manager.LoadOrder().empty());
}

TEST_F(ModRuntime, CorruptOrMissingModStateDoesNotReplaceCurrentState) {
    Add("return {on_tick=function() chronicle.set('value',9) end}");
    Start();
    runtime.Tick({1, 0, 0, true});
    const auto  before = runtime.SaveState();
    std::string error;
    EXPECT_FALSE(runtime.LoadState("{bad", error));
    EXPECT_EQ(runtime.SaveState(), before);
    auto bad = Json::parse(before);
    bad["required"]["test.missing"] = "1";
    EXPECT_FALSE(runtime.LoadState(bad.dump(), error));
    EXPECT_EQ(runtime.SaveState(), before);
}

TEST_F(ModRuntime, SaveRoundTripIsAtomicAndKeepsNativePrefix) {
    Add("return {on_tick=function() chronicle.set('saved',true) end}");
    Start();
    mods::Scripts().Start(manager);
    mods::Scripts().Tick({1, 0, 0, true});
    const auto        state = mods::Scripts().SaveState();
    const std::string native = "native-save-image";
    const auto        path = root / "save.dat";
    EXPECT_EQ(mods::WriteSave(path, native.data(), native.size(), false), FilesResult::kWritten);
    std::ifstream f(path, std::ios::binary);
    std::string   prefix(native.size(), '\0');
    f.read(prefix.data(), prefix.size());
    EXPECT_EQ(prefix, native);
    mods::Scripts().ResetState();
    std::string error;
    EXPECT_TRUE(mods::LoadSaveState(path, native.size(), error)) << error;
    EXPECT_EQ(mods::Scripts().SaveState(), state);
    EXPECT_EQ(mods::WriteSave(path, "different", 9, false), FilesResult::kExists);
    EXPECT_TRUE(mods::LoadSaveState(path, native.size(), error));
    EXPECT_EQ(mods::Scripts().SaveState(), state);
    f.close();
    std::fstream damaged(path, std::ios::binary | std::ios::in | std::ios::out);
    damaged.seekp(native.size() + 25);
    damaged.put('X');
    damaged.close();
    EXPECT_FALSE(mods::LoadSaveState(path, native.size(), error));
    EXPECT_NE(error.find("checksum"), std::string::npos);
    EXPECT_EQ(mods::Scripts().SaveState(), state);
}

TEST_F(ModRuntime, ProfileTogglesPreviewWithoutChangingActiveSession) {
    Add("return {}");
    mods::Session session;
    session.Start({root / "mods"}, {}, root / "save");
    ASSERT_TRUE(session.Active().Packages()[0].Active());
    ASSERT_TRUE(session.SetEnabled("test.mod", false)) << session.Error();
    EXPECT_TRUE(session.Active().Packages()[0].Active());
    EXPECT_FALSE(session.Preview().Packages()[0].enabled);
    EXPECT_TRUE(session.Pending());
    mods::Session restart;
    restart.Start({root / "mods"}, {}, root / "save");
    EXPECT_FALSE(restart.Active().Packages()[0].enabled);
    ASSERT_TRUE(session.SetEnabled("test.mod", true));
    EXPECT_FALSE(session.Pending());
}

TEST_F(ModRuntime, ToggleRecomputesConflictReasons) {
    auto a = Manifest();
    a["conflicts"] = {"test.other"};
    Add("return {}", a);
    Add("return {}", Manifest("test.other"));
    mods::Session session;
    session.Start({root / "mods"}, {}, root / "save");
    ASSERT_FALSE(session.Preview().Packages()[0].problems.empty());
    ASSERT_TRUE(session.SetEnabled("test.other", false));
    for (const auto &p : session.Preview().Packages()) {
        if (p.id == "test.mod") {
            EXPECT_TRUE(p.problems.empty());
        }
    }
}

TEST_F(ModRuntime, ProfileWriteFailureDoesNotChangeSelection) {
    Add("return {}");
    mods::Session session;
    session.Start({root / "mods"}, {}, root / "save");
    fs::create_directories(root / "save/mods-profile.json");
    EXPECT_FALSE(session.SetEnabled("test.mod", false));
    EXPECT_TRUE(session.Preview().Packages()[0].enabled);
    EXPECT_FALSE(session.Pending());
}

TEST_F(ModRuntime, CorruptProfileIsNotOverwritten) {
    Add("return {}");
    Write(root / "save/mods-profile.json", "{bad");
    mods::Session session;
    session.Start({root / "mods"}, {}, root / "save");
    EXPECT_FALSE(session.Error().empty());
    EXPECT_FALSE(session.Active().Packages()[0].Active());
    EXPECT_FALSE(session.SetEnabled("test.mod", true));
}
} // namespace
