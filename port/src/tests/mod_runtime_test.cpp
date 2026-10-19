#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "mods/api.hpp"
#include "mods/native.hpp"
#include "mods/runtime.hpp"
#include "mods/save.hpp"
#include "mods/session.hpp"
#include "userstatus.hpp"

namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;

class ModRuntime : public testing::Test {
protected:
    fs::path                    root = fs::temp_directory_path() / ("chronicle-runtime-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    mods::Manager               manager;
    mods::Runtime               runtime;
    std::map<std::string, bool> enabled;

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
        enabled[manifest["id"].get<std::string>()] = true;
        const auto path = root / "mods" / manifest["id"].get<std::string>();
        Write(path / "mod.json", manifest.dump());
        Write(path / "main.lua", code);
    }

    void Start() {
        manager.Scan({root / "mods"}, {}, enabled);
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
    Write(root / "save/mods-profile.json", Json{
                                               {"schema_version", 1      },
                                               {"enabled",        enabled}
    }
                                               .dump());
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
    Write(root / "save/mods-profile.json", Json{
                                               {"schema_version", 1      },
                                               {"enabled",        enabled}
    }
                                               .dump());
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
    Write(root / "save/mods-profile.json", Json{
                                               {"schema_version", 1      },
                                               {"enabled",        enabled}
    }
                                               .dump());
    session.Start({root / "mods"}, {}, root / "save");
    fs::remove(root / "save/mods-profile.json");
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

TEST_F(ModRuntime, FreshInstallCannotEnableItselfAndHasNoGameplayEffects) {
    auto manifest = Manifest();
    manifest["enabled"] = true;
    Add("error('must never run')", manifest);
    mods::Session session;
    session.Start({root / "mods"}, {}, root / "save");
    ASSERT_FALSE(session.Active().Packages()[0].Active());
    runtime.Start(session.Active());
    EXPECT_FALSE(runtime.HasGameplay());
    int writes = 0, reads = 0;
    runtime.Tick({1, 0, 0, true}, [&](int) { ++writes; return true; }, [&](std::string_view) { ++reads; return Json::object(); });
    EXPECT_EQ(writes, 0);
    EXPECT_EQ(reads, 0);
    EXPECT_TRUE(runtime.SaveState().empty());
    EXPECT_TRUE(runtime.Diagnostics().empty());
    ASSERT_TRUE(session.SetEnabled("test.mod", true));
    EXPECT_FALSE(session.Active().Packages()[0].Active());
    mods::Session restart;
    restart.Start({root / "mods"}, {}, root / "save");
    EXPECT_TRUE(restart.Active().Packages()[0].Active());
}

TEST_F(ModRuntime, ReadApisArePermissionGatedAndLazy) {
    auto manifest = Manifest();
    manifest["permissions"] = {"game.read"};
    Add(R"(return {on_tick=function()
        local p=assert(chronicle.player()); assert(p.hp==42); p.hp=1
        assert(chronicle.party().members[1].id==0)
        assert(chronicle.inventory().slots[1].id==7)
    end})",
        manifest);
    Start();
    int  calls = 0;
    Json player = {
        {"hp", 42}
    };
    runtime.Tick({1, 0, 0, false}, {}, [&](std::string_view) { ++calls; return player; });
    EXPECT_EQ(calls, 0);
    runtime.Tick({1, 0, 0, true}, {}, [&](std::string_view service) {
        ++calls;
        if (service == "player") {
            return player;
        }
        if (service == "party") {
            return Json{
                {"members", Json::array({{{"id", 0}}})}
            };
        }
        return Json{
            {"slots", Json::array({{{"id", 7}}})}
        };
    });
    EXPECT_EQ(calls, 3);
    EXPECT_EQ(player["hp"], 42);
    EXPECT_TRUE(runtime.Diagnostics().empty());
    manifest["permissions"] = Json::array();
    Add("return {on_tick=function() local p,e=chronicle.player(); assert(p==nil and e) end}", manifest);
    Start();
    calls = 0;
    runtime.Tick({1, 0, 0, true}, {}, [&](std::string_view) { ++calls; return player; });
    EXPECT_EQ(calls, 0);
    EXPECT_TRUE(runtime.Diagnostics().empty());
}

TEST_F(ModRuntime, ResourcesAreOwnedBoundedAndCached) {
    auto manifest = Manifest();
    manifest["permissions"] = {"resources.read"};
    manifest["resources"] = {
        {"greeting", "data/greeting.txt"}
    };
    Write(root / "mods/test.mod/data/greeting.txt", "hello");
    Add(R"(return {on_tick=function()
        assert(chronicle.resource('greeting')=='hello')
        local v,e=chronicle.resource('../secret'); assert(v==nil and e)
    end})",
        manifest);
    Start();
    Write(root / "mods/test.mod/data/greeting.txt", "changed");
    runtime.Tick({1, 0, 0, true});
    EXPECT_TRUE(runtime.Diagnostics().empty());
    Write(root / "mods/test.mod/data/greeting.txt", std::string(65537, 'x'));
    Start();
    EXPECT_FALSE(manager.Packages()[0].problems.empty());
    EXPECT_FALSE(runtime.HasGameplay());
}

TEST_F(ModRuntime, LogsAreBoundedAndDoNotBecomeSaveState) {
    auto manifest = Manifest();
    manifest.erase("quests");
    Add(R"(for i=1,150 do assert(chronicle.log('info','message')) end
        assert(not chronicle.log('unknown','message'))
        assert(not chronicle.log('info',string.rep('x',513)))
        return {})",
        manifest);
    Start();
    ASSERT_EQ(runtime.Logs().size(), 128u);
    EXPECT_TRUE(runtime.Logs()[0].starts_with("test.mod: [info]"));
    EXPECT_TRUE(State().empty());
    EXPECT_TRUE(runtime.Diagnostics().empty());
}

TEST_F(ModRuntime, HostCallsRollbackOnScriptFailureAndRequirePermissions) {
    auto manifest = Manifest();
    manifest["permissions"] = {"game.read", "game.write", "economy.write"};
    Add(R"(return {on_tick=function()
        assert(chronicle.api_available('player.heal'))
        assert(not chronicle.api_available('entities.spawn'))
        assert(chronicle.call('player.heal',{amount=12}))
        assert(chronicle.call('economy.spend',{amount=1}))
        chronicle.set('should_rollback',true)
        error('abort')
    end})",
        manifest);
    Start();
    int commits = 0;
    runtime.Tick({1, 0, 20, true}, {}, {}, {}, [&](const auto &, int, std::string &) { ++commits; return true; });
    EXPECT_EQ(commits, 0);
    EXPECT_FALSE(runtime.Diagnostics().empty());
    EXPECT_FALSE(State().value("values", Json::object()).contains("should_rollback"));
    manifest["permissions"] = Json::array();
    Add("return {on_tick=function() assert(not chronicle.call('player.heal',{amount=1})) end}", manifest);
    Start();
    runtime.Tick({1, 0, 20, true}, {}, {}, {}, [&](const auto &, int, std::string &) { ++commits; return true; });
    EXPECT_EQ(commits, 0);
    EXPECT_TRUE(runtime.Diagnostics().empty());
}

TEST_F(ModRuntime, HostBatchRejectsWithoutLeavingModStateAheadOfNativeState) {
    auto manifest = Manifest();
    manifest["permissions"] = {"game.write", "economy.write"};
    Add("return {on_tick=function() chronicle.set('paid',true); assert(chronicle.call('player.heal',{amount=10})); assert(chronicle.call('economy.spend',{amount=100})) end}", manifest);
    Start();
    CUserStatus status{};
    status.party_size = 1;
    status.hp[0] = 20;
    status.max_hp[0] = 100;
    status.money = 5;
    runtime.Tick({1, 0, 5, true}, {}, {}, {}, [&](const auto &commands, int money, std::string &error) { return mods::ApplyStatus(status, commands, money, error); });
    EXPECT_EQ(status.hp[0], 20);
    EXPECT_EQ(status.money, 5);
    EXPECT_FALSE(runtime.HasGameplay());
    EXPECT_FALSE(State().value("values", Json::object()).contains("paid"));
}

TEST_F(ModRuntime, NativePluginRequiresSeparateTrustAndEnableAndRejectsChangedBytes) {
    auto manifest = Manifest("test.native");
    manifest.erase("script");
    manifest.erase("quests");
    manifest["permissions"] = {"native.extension", "game.read", "economy.write"};
    const fs::path source = CHRONICLE_NATIVE_FIXTURE;
    const auto     name = "plugin" + source.extension().string();
    manifest["native_library"] = name;
    const auto package = root / "mods/test.native";
    Write(package / "mod.json", manifest.dump());
    fs::copy_file(source, package / name);
    mods::Session session;
    session.Start({root / "mods"}, {}, root / "save");
    runtime.Start(session.Active());
    EXPECT_FALSE(runtime.HasGameplay());
    EXPECT_TRUE(runtime.Logs().empty());
    const auto fingerprint = session.Preview().Packages()[0].native_fingerprint;
    ASSERT_TRUE(session.SetTrusted("test.native", fingerprint, true));
    mods::Session trusted;
    trusted.Start({root / "mods"}, {}, root / "save");
    runtime.Start(trusted.Active());
    EXPECT_FALSE(runtime.HasGameplay());
    ASSERT_TRUE(trusted.SetEnabled("test.native", true));
    mods::Session enabled_session;
    enabled_session.Start({root / "mods"}, {}, root / "save");
    runtime.Start(enabled_session.Active());
    ASSERT_TRUE(runtime.HasGameplay());
    ASSERT_EQ(runtime.Logs().size(), 1u);
    CUserStatus status{};
    status.money = 5;
    runtime.Tick({1, 0, 5, true}, {}, {}, [&](auto op, const auto &args) { return mods::ReadStatus(status, {1, 0, 5, true}, op, args); }, [&](const auto &commands, int money, std::string &error) { return mods::ApplyStatus(status, commands, money, error); });
    EXPECT_EQ(status.money, 12);
    EXPECT_TRUE(runtime.Diagnostics().empty());
    runtime.Tick({1, 0, 12, true}, {}, {}, [](auto, const auto &) { return Json::object(); }, [](const auto &, int, std::string &) { return false; });
    EXPECT_FALSE(runtime.HasGameplay());
    EXPECT_EQ(runtime.Logs().back(), "test.native: [native] fixture stopped");
    std::ofstream changed(package / name, std::ios::binary | std::ios::app);
    changed.put('x');
    changed.close();
    enabled_session.Refresh();
    EXPECT_FALSE(enabled_session.Preview().Packages()[0].Active());
    EXPECT_FALSE(enabled_session.SetTrusted("test.native", fingerprint, true));
}

} // namespace
