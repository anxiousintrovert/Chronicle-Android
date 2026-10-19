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

TEST_F(ModRuntime, HostIntegerIdsRoundTripIntoValidatedWrites) {
    auto manifest = Manifest();
    manifest["permissions"] = {"game.read", "game.write"};
    Add(R"(return {on_tick=function()
        local p=assert(chronicle.call('georama.elements_get',{town=0,plot=0}))
        assert(math.type(p.elements[1].element)=='integer')
        assert(p.signed==-9223372036854775807)
        assert(math.type(p.fraction)=='float')
        assert(chronicle.call('georama.element_set',{town=p.town,plot=p.plot,element=p.elements[1].element,enabled=true}))
    end})",
        manifest);
    Start();
    int commits = 0;
    runtime.Tick({1, 0, 0, true}, {}, {}, [](std::string_view, const Json &) { return Json{
                                                                                   {"town",     0                              },
                                                                                   {"plot",     uint64_t(0)                    },
                                                                                   {"elements", Json::array({{{"element", 1}}})},
                                                                                   {"signed",   int64_t(-9223372036854775807LL)},
                                                                                   {"fraction", 0.5                            }
 }; }, [&](const std::vector<mods::GameCommand> &commands, int, std::string &) {
            ++commits;
            EXPECT_EQ(commands.size(), 1);
            EXPECT_TRUE(commands.at(0).arguments.at("element").is_number_integer());
            EXPECT_EQ(commands.at(0).arguments.at("element"), 1);
            return true; });
    EXPECT_EQ(commits, 1);
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

TEST_F(ModRuntime, OwnedListenersFollowLifecycleWithoutRepeatingFloorsOnMenuReturn) {
    Add(R"(
        for _,event in ipairs({'tick','map_enter','floor_enter','game_enter','game_leave'}) do
            assert(chronicle.subscribe(event,function(w)
                chronicle.set(event,(chronicle.get(event) or 0)+1)
                if event=='floor_enter' then chronicle.set('floor',w.floor) end
                if event=='game_leave' then assert(not w.playing); assert(chronicle.add_money(1)==nil) end
            end))
        end
        return {on_tick=function() chronicle.set('legacy',(chronicle.get('legacy') or 0)+1) end}
    )");
    Start();
    runtime.Tick({1, 0, 0, true, 0, 1});
    runtime.Tick({1, 0, 0, true, 0, 1});
    runtime.Tick({2, 0, 0, false});
    runtime.Tick({1, 0, 0, true, 0, 1});
    runtime.Tick({1, 0, 0, true, 0, 2});
    EXPECT_TRUE(runtime.Diagnostics().empty());
    const auto values = State()["values"];
    EXPECT_EQ(values["tick"], 4);
    EXPECT_EQ(values["legacy"], 4);
    EXPECT_EQ(values["map_enter"], 1);
    EXPECT_EQ(values["floor_enter"], 2);
    EXPECT_EQ(values["game_enter"], 2);
    EXPECT_EQ(values["game_leave"], 1);
    EXPECT_EQ(values["floor"], 2);
    mods::Manager empty;
    empty.Scan({}, {});
    runtime.Start(empty);
    EXPECT_FALSE(runtime.HasGameplay());
}

TEST_F(ModRuntime, SaveLoadedEventWaitsForGameplayAndUsesRestoredOwnedState) {
    Add(R"(
        assert(chronicle.subscribe('save_loaded',function()
            chronicle.set('loads',(chronicle.get('loads') or 0)+1)
        end))
        return {}
    )");
    Start();
    runtime.Tick({1, 0, 0, true});
    std::string error;
    ASSERT_TRUE(runtime.LoadState(runtime.SaveState(), error)) << error;
    runtime.Tick({0, 0, 0, false});
    EXPECT_FALSE(State().contains("values"));
    runtime.Tick({1, 0, 0, true});
    runtime.Tick({1, 0, 0, true});
    EXPECT_EQ(State()["values"]["loads"], 1);
    ASSERT_TRUE(runtime.LoadState(runtime.SaveState(), error)) << error;
    runtime.Tick({1, 0, 0, true});
    EXPECT_EQ(State()["values"]["loads"], 2);
}

TEST_F(ModRuntime, OneEventRollsBackAllItsListenersAndQueuedWritesOnError) {
    auto manifest = Manifest();
    manifest["permissions"] = {"economy.write"};
    Add(R"(
        assert(chronicle.subscribe('tick',function() assert(chronicle.set('partial',1)); assert(chronicle.add_money(2)) end))
        assert(chronicle.subscribe('tick',function() error('listener failed') end))
        return {}
    )",
        manifest);
    Start();
    int commits = 0;
    runtime.Tick({1, 0, 0, true}, [&](int) {++commits;return true; });
    EXPECT_EQ(commits, 0);
    EXPECT_FALSE(runtime.Diagnostics().empty());
    EXPECT_FALSE(State().contains("values"));
}

TEST_F(ModRuntime, QueryExportsRequireDependenciesAndReturnDetachedStructuredResults) {
    auto provider = Manifest("test.provider");
    Add(R"(
        local result={answer=7,nested={id='author.blade'}}
        assert(chronicle.export_query('read',function(a) assert(a.request=='value'); return result end))
        assert(chronicle.export_query('fail',function() error('expected export failure') end))
        return {}
    )",
        provider);
    auto consumer = Manifest();
    consumer["requires"] = {"test.provider"};
    Add(R"(return {on_tick=function()
        local v,e=chronicle.invoke_query('test.provider','fail',{})
        assert(v==nil and e)
        local a=assert(chronicle.invoke_query('test.provider','read',{request='value'}))
        assert(a.answer==7 and a.nested.id=='author.blade'); a.nested.id='changed'
        assert(chronicle.invoke_query('test.provider','read',{request='value'}).nested.id=='author.blade')
        assert(chronicle.subscribe('tick',function() end)==nil)
        assert(chronicle.export_query('late',function() return {} end)==nil)
        chronicle.set('passed',true)
    end})",
        consumer);
    Start();
    runtime.Tick({1, 0, 0, true});
    EXPECT_TRUE(runtime.Diagnostics().empty());
    EXPECT_EQ(State()["values"]["passed"], true);
    consumer["requires"] = Json::array();
    Add(R"(return {on_tick=function() local v,e=chronicle.invoke_query('test.provider','read',{}) assert(v==nil and e:find('declared dependency')) end})", consumer);
    Start();
    runtime.Tick({1, 0, 0, true});
    EXPECT_TRUE(runtime.Diagnostics().empty());
}

TEST_F(ModRuntime, QueryExportsCannotMutateProviderStateOrGameButCanReadWithProviderPermissions) {
    auto provider = Manifest("test.provider");
    provider["permissions"] = {"game.read", "game.write", "economy.write"};
    Add(R"(
        assert(chronicle.export_query('read',function()
            assert(chronicle.set('bad',1)==nil)
            assert(chronicle.quest_add('visit',1)==nil)
            assert(chronicle.add_money(1)==nil)
            assert(chronicle.call('player.heal',{amount=1})==nil)
            return assert(chronicle.call('economy.get'))
        end))
        return {}
    )",
        provider);
    auto consumer = Manifest();
    consumer["requires"] = {"test.provider"};
    Add(R"(return {on_tick=function() assert(chronicle.invoke_query('test.provider','read',{}).money==123) end})", consumer);
    Start();
    int reads = 0, commits = 0;
    runtime.Tick({1, 0, 123, true}, {}, {}, [&](auto op, const auto &) {++reads; EXPECT_EQ(op,"economy.get"); return Json{{"money",123}}; }, [&](const auto &, int, std::string &) {++commits;return true; });
    EXPECT_TRUE(runtime.Diagnostics().empty());
    EXPECT_EQ(reads, 1);
    EXPECT_EQ(commits, 0);
    EXPECT_FALSE(Json::parse(runtime.SaveState())["mods"]["test.provider"].contains("values"));
}

TEST_F(ModRuntime, QueryInvocationBudgetAndBadResultsFailWithoutPoisoningOtherCallbacks) {
    Add(R"(
        assert(chronicle.export_query('ok',function() return {} end))
        assert(chronicle.export_query('bad',function() return function() end end))
        return {}
    )",
        Manifest("test.provider"));
    auto consumer = Manifest();
    consumer["requires"] = {"test.provider"};
    Add(R"(return {on_tick=function()
        local v,e=chronicle.invoke_query('test.provider','bad',{}) assert(v==nil and e)
        for i=1,15 do assert(chronicle.invoke_query('test.provider','ok',{})) end
        local v,e=chronicle.invoke_query('test.provider','ok',{}) assert(v==nil and e:find('limit'))
        chronicle.set('ticks',(chronicle.get('ticks') or 0)+1)
    end})",
        consumer);
    Start();
    runtime.Tick({1, 0, 0, true});
    runtime.Tick({1, 0, 0, true});
    EXPECT_TRUE(runtime.Diagnostics().empty());
    EXPECT_EQ(State()["values"]["ticks"], 2);
}

TEST_F(ModRuntime, NestedQueryExportsShareBudgetAndSurviveSaveRuntimeReplacement) {
    Add(R"(assert(chronicle.export_query('read',function() return {value=21} end)); return {})", Manifest("test.base"));
    auto middle = Manifest("test.middle");
    middle["requires"] = {"test.base"};
    Add(R"(assert(chronicle.export_query('read',function() return assert(chronicle.invoke_query('test.base','read',{})) end)); return {})", middle);
    auto consumer = Manifest();
    consumer["requires"] = {"test.middle"};
    Add(R"(return {on_tick=function()
        for i=1,8 do assert(chronicle.invoke_query('test.middle','read',{}).value==21) end
        local v,e=chronicle.invoke_query('test.middle','read',{}); assert(v==nil and e:find('limit'))
        chronicle.set('ticks',(chronicle.get('ticks') or 0)+1)
    end})",
        consumer);
    Start();
    runtime.Tick({1, 0, 0, true});
    ASSERT_TRUE(runtime.Diagnostics().empty());
    EXPECT_EQ(State()["values"]["ticks"], 1);
    std::string error;
    ASSERT_TRUE(runtime.LoadState(runtime.SaveState(), error)) << error;
    runtime.Tick({1, 0, 0, true});
    EXPECT_TRUE(runtime.Diagnostics().empty());
    EXPECT_EQ(State()["values"]["ticks"], 2);
}

TEST_F(ModRuntime, ListenerAndExportRegistrationAreBoundedAndOwnedByRuntimeLifetime) {
    Add(R"(
        assert(chronicle.subscribe('not_an_event',function() end)==nil)
        for i=1,32 do
            assert(chronicle.subscribe('tick',function() end))
            assert(chronicle.export_query('query'..i,function() return {} end))
        end
        assert(chronicle.subscribe('tick',function() end)==nil)
        assert(chronicle.export_query('extra',function() return {} end)==nil)
        assert(chronicle.export_query('query1',function() return {} end)==nil)
        return {}
    )");
    Start();
    ASSERT_TRUE(runtime.Diagnostics().empty());
    runtime.Tick({1, 0, 0, true});
    EXPECT_TRUE(runtime.Diagnostics().empty());
    runtime.ResetState();
    runtime.Tick({1, 0, 0, true});
    EXPECT_TRUE(runtime.Diagnostics().empty());
}

TEST_F(ModRuntime, MenuActionQueuesOnceAndCommitsOnlyOnGameplayTick) {
    auto manifest = Manifest();
    manifest["permissions"] = {"economy.write"};
    manifest["requires_capabilities"] = {"menu.actions.v1"};
    Add(R"(
        assert(chronicle.action('reward','Reward','Add two coins.',function(w)
            assert(w.playing and w.map==0)
            assert(chronicle.set('runs',(chronicle.get('runs') or 0)+1))
            assert(chronicle.add_money(2))
        end))
        return {on_tick=function() assert(chronicle.action('late','Late','Late',function() end)==nil) end}
    )",
        manifest);
    Start();
    ASSERT_TRUE(runtime.Diagnostics().empty());
    ASSERT_EQ(runtime.Actions("test.mod").size(), 1u);
    EXPECT_FALSE(runtime.Actions("test.mod")[0].available);
    std::string error;
    EXPECT_FALSE(runtime.QueueAction("test.mod", "reward", error));
    int        total = 0;
    const auto commit = [&](int amount) { total += amount; return true; };
    runtime.Tick({1, 0, 0, true}, commit);
    EXPECT_TRUE(runtime.Actions("test.mod")[0].available);
    ASSERT_TRUE(runtime.QueueAction("test.mod", "reward", error)) << error;
    EXPECT_EQ(total, 0);
    EXPECT_FALSE(State().contains("values"));
    EXPECT_FALSE(runtime.QueueAction("test.mod", "reward", error));
    EXPECT_EQ(error, "Action is already queued.");
    EXPECT_FALSE(runtime.QueueAction("test.mod", "unknown", error));
    runtime.Tick({1, 0, 0, true}, commit);
    EXPECT_EQ(total, 2);
    EXPECT_EQ(State()["values"]["runs"], 1);
    EXPECT_EQ(runtime.Actions("test.mod")[0].result, "Completed.");
    runtime.Tick({1, 0, 0, true}, commit);
    EXPECT_EQ(total, 2);
    EXPECT_TRUE(runtime.Diagnostics().empty());
}

TEST_F(ModRuntime, MenuActionsRollbackOwnedStateAndWritesOnCallbackOrCommitFailure) {
    auto manifest = Manifest();
    manifest["permissions"] = {"economy.write"};
    for (const bool callback_error : {true, false}) {
        Add("assert(chronicle.action('fail','Fail','Failure test',function() chronicle.set('partial',1); assert(chronicle.add_money(3)); " +
                std::string(callback_error ? "error('action failure')" : "") + " end)); return {}",
            manifest);
        Start();
        runtime.Tick({1, 0, 0, true});
        std::string error;
        ASSERT_TRUE(runtime.QueueAction("test.mod", "fail", error));
        int commits = 0;
        runtime.Tick({1, 0, 0, true}, [&](int) { ++commits; return false; });
        EXPECT_EQ(commits, callback_error ? 0 : 1);
        EXPECT_FALSE(State().contains("values"));
        EXPECT_FALSE(runtime.Diagnostics().empty());
        EXPECT_FALSE(runtime.Actions("test.mod")[0].available);
        EXPECT_EQ(runtime.Actions("test.mod")[0].result, "Failed. See module diagnostics.");
        EXPECT_FALSE(runtime.QueueAction("test.mod", "fail", error));
    }
}

TEST_F(ModRuntime, MenuActionsCancelOnGameplayExitLoadResetAndDisable) {
    Add("assert(chronicle.action('run','Run','Run once',function() chronicle.set('ran',true) end)); return {}");
    Start();
    runtime.Tick({1, 0, 0, true});
    std::string error;
    ASSERT_TRUE(runtime.QueueAction("test.mod", "run", error));
    runtime.Tick({0, 0, 0, false});
    EXPECT_TRUE(runtime.Actions("test.mod")[0].result.starts_with("Canceled:"));
    runtime.Tick({1, 0, 0, true});
    EXPECT_FALSE(State().contains("values"));
    ASSERT_TRUE(runtime.QueueAction("test.mod", "run", error));
    ASSERT_TRUE(runtime.LoadState(runtime.SaveState(), error)) << error;
    runtime.Tick({1, 0, 0, true});
    EXPECT_FALSE(State().contains("values"));
    EXPECT_TRUE(runtime.Actions("test.mod")[0].result.empty());
    ASSERT_TRUE(runtime.QueueAction("test.mod", "run", error));
    runtime.ResetState();
    runtime.Tick({1, 0, 0, true});
    EXPECT_FALSE(State().contains("values"));
    ASSERT_TRUE(runtime.QueueAction("test.mod", "run", error));
    enabled["test.mod"] = false;
    Start();
    runtime.Tick({1, 0, 0, true});
    EXPECT_TRUE(runtime.Actions("test.mod").empty());
    EXPECT_FALSE(runtime.QueueAction("test.mod", "run", error));
    EXPECT_FALSE(runtime.HasGameplay());
}

TEST_F(ModRuntime, MenuActionRegistrationRejectsInvalidMetadataAndEnforcesBounds) {
    Add(R"(
        local function f() end
        assert(chronicle.action('bad id','Title','Description',f)==nil)
        assert(chronicle.action('bad\0id','Title','Description',f)==nil)
        assert(chronicle.action('bad','','Description',f)==nil)
        assert(chronicle.action('bad','Title','new\nline',f)==nil)
        assert(chronicle.action('bad','Title','nul\0text',f)==nil)
        assert(chronicle.action('bad',string.char(255),'Description',f)==nil)
        assert(chronicle.action('bad','Title',string.rep('a',257),f)==nil)
        assert(chronicle.action('bad','Title','Description',{})==nil)
        for i=1,32 do assert(chronicle.action('action'..i,'Literal {cross}','Description',f)) end
        assert(chronicle.action('action1','Title','Description',f)==nil)
        assert(chronicle.action('extra','Title','Description',f)==nil)
        return {}
    )");
    Start();
    ASSERT_TRUE(runtime.Diagnostics().empty());
    EXPECT_EQ(runtime.Actions("test.mod").size(), 32u);
    EXPECT_TRUE(runtime.Actions("unknown").empty());
    runtime.Tick({1, 0, 0, true});
    std::string error;
    for (int i = 1; i <= 16; ++i) {
        ASSERT_TRUE(runtime.QueueAction("test.mod", "action" + std::to_string(i), error));
    }
    EXPECT_FALSE(runtime.QueueAction("test.mod", "action17", error));
    EXPECT_EQ(error, "At most 16 queued actions.");
    runtime.Tick({1, 0, 0, true});
    EXPECT_TRUE(runtime.QueueAction("test.mod", "action17", error));
}

TEST_F(ModRuntime, MenuActionsUseDependencyOrderAndDoNotRunAfterDependencyFailure) {
    auto provider = Manifest("test.provider");
    Add(R"(
        assert(chronicle.action('set','Set','Set value',function() chronicle.set('value',42) end))
        assert(chronicle.export_query('get',function() return {value=chronicle.get('value')} end))
        return {}
    )",
        provider);
    auto consumer = Manifest();
    consumer["requires"] = {"test.provider"};
    Add(R"(
        assert(chronicle.action('read','Read','Read provider',function()
            chronicle.set('value',assert(chronicle.invoke_query('test.provider','get',{})).value)
        end)); return {}
    )",
        consumer);
    Start();
    runtime.Tick({1, 0, 0, true});
    std::string error;
    ASSERT_TRUE(runtime.QueueAction("test.mod", "read", error));
    ASSERT_TRUE(runtime.QueueAction("test.provider", "set", error));
    runtime.Tick({1, 0, 0, true});
    EXPECT_TRUE(runtime.Diagnostics().empty());
    EXPECT_EQ(State()["values"]["value"], 42);
    Add("assert(chronicle.action('set','Set','Fail provider',function() error('failed') end)); return {}", provider);
    Start();
    runtime.Tick({1, 0, 0, true});
    ASSERT_TRUE(runtime.QueueAction("test.mod", "read", error));
    ASSERT_TRUE(runtime.QueueAction("test.provider", "set", error));
    runtime.Tick({1, 0, 0, true});
    EXPECT_FALSE(runtime.Diagnostics().empty());
    EXPECT_TRUE(runtime.Actions("test.mod")[0].result.starts_with("Canceled:"));
    EXPECT_FALSE(runtime.Actions("test.mod")[0].available);
}

TEST_F(ModRuntime, MenuActionsCancelOnWorldChangeAndKeepNormalCallbackBudgets) {
    Add(R"(
        assert(chronicle.action('run','Run','Run once',function() chronicle.set('ran',true) end))
        assert(chronicle.action('loop','Loop','Budget failure',function() while true do end end))
        return {}
    )");
    Start();
    runtime.Tick({1, 0, 0, true, 0, 1});
    std::string error;
    ASSERT_TRUE(runtime.QueueAction("test.mod", "run", error));
    runtime.Tick({1, 0, 0, true, 0, 2});
    EXPECT_FALSE(State().contains("values"));
    EXPECT_TRUE(runtime.Actions("test.mod")[1].result.starts_with("Canceled:"));
    ASSERT_TRUE(runtime.QueueAction("test.mod", "loop", error));
    runtime.Tick({1, 0, 0, true, 0, 2});
    EXPECT_FALSE(runtime.Diagnostics().empty());
    EXPECT_FALSE(runtime.HasGameplay());
    EXPECT_FALSE(runtime.QueueAction("test.mod", "run", error));
}

class ModNativeV2 : public ModRuntime {
protected:
    void LoadPlugin(const std::string &mode = {}, const fs::path &source = CHRONICLE_NATIVE_V2_FIXTURE, bool trust = true, bool enable = true, const std::string &script = {}) {
        mods::Manager empty;
        runtime.Start(empty);
        runtime.ResetState();
        auto manifest = Manifest("test.native.v2");
        manifest.erase("quests");
        manifest["permissions"] = {"native.extension", "game.read", "economy.write"};
        manifest["requires_capabilities"] = {"plugins.native.v2"};
        manifest["native_library"] = "plugin" + source.extension().string();
        const auto package = root / "mods/test.native.v2";
        if (mode.empty() && script.empty()) {
            manifest.erase("script");
        } else {
            Write(package / "main.lua", "assert(chronicle.set('mode','" + mode + "')); " + script + "; return {}");
        }
        Write(package / "mod.json", manifest.dump());
        fs::copy_file(source, package / manifest["native_library"].get<std::string>(), fs::copy_options::overwrite_existing);
        mods::Session preview;
        preview.Start({root / "mods"}, {}, root / "save");
        ASSERT_EQ(preview.Preview().Packages().size(), 1u);
        ASSERT_TRUE(preview.SetTrusted("test.native.v2", preview.Preview().Packages()[0].native_fingerprint, trust));
        ASSERT_TRUE(preview.SetEnabled("test.native.v2", enable));
        mods::Session active;
        active.Start({root / "mods"}, {}, root / "save");
        runtime.Start(active.Active());
    }

    Json Values() { return Json::parse(runtime.SaveState())["mods"]["test.native.v2"]["values"]; }

    void TearDown() override {
        mods::Manager empty;
        runtime.Start(empty);
        ModRuntime::TearDown();
    }
};

TEST_F(ModNativeV2, LifecycleEventsAreOwnedOrderedAndLeaveCannotCallGame) {
    LoadPlugin();
    ASSERT_TRUE(runtime.HasGameplay());
    ASSERT_TRUE(runtime.Diagnostics().empty());
    EXPECT_FALSE(Json::parse(runtime.SaveState())["mods"]["test.native.v2"].contains("values"));
    int        money = 0;
    const auto commit = [&](const std::vector<mods::GameCommand> &commands, int, std::string &) {
        for (const auto &command : commands) {
            money += command.arguments.at("amount").get<int>();
        }
        return true;
    };
    runtime.Tick({1, 0, 0, true, 0, 1}, {}, {}, {}, commit);
    runtime.Tick({1, 0, 7, true, 0, 1}, {}, {}, {}, commit);
    runtime.Tick({1, 0, 14, true, 0, 2}, {}, {}, {}, commit);
    runtime.Tick({0, 0, 21, false}, {}, {}, {}, commit);
    EXPECT_TRUE(runtime.Diagnostics().empty());
    const auto state = Values();
    EXPECT_EQ(state["game_enter"], 1);
    EXPECT_EQ(state["game_leave"], 1);
    EXPECT_EQ(state["map_enter"], 1);
    EXPECT_EQ(state["floor_enter"], 2);
    EXPECT_EQ(state["ticks"], 3);
    EXPECT_EQ(money, 21);
    EXPECT_EQ(runtime.Logs(), (std::vector<std::string>{
                                  "test.native.v2: [native] v2 started", "test.native.v2: [native] game_enter", "test.native.v2: [native] map_enter",
                                  "test.native.v2: [native] floor_enter", "test.native.v2: [native] v2 tick", "test.native.v2: [native] v2 tick",
                                  "test.native.v2: [native] floor_enter", "test.native.v2: [native] v2 tick", "test.native.v2: [native] game_leave"}));
}

TEST_F(ModNativeV2, SaveLoadRebindsOwnedValuesBeforeDeferredEventAndResetClearsThem) {
    LoadPlugin();
    const auto commit = [](const auto &, int, std::string &) { return true; };
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
    const auto  saved = runtime.SaveState();
    std::string error;
    ASSERT_TRUE(runtime.LoadState(saved, error)) << error;
    runtime.Tick({0, 0, 0, false}, {}, {}, {}, commit);
    EXPECT_EQ(Values()["ticks"], 2);
    EXPECT_FALSE(Values().contains("save_loaded"));
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
    EXPECT_TRUE(runtime.Diagnostics().empty());
    EXPECT_EQ(Values()["ticks"], 3);
    EXPECT_EQ(Values()["save_loaded"], 1);
    EXPECT_EQ(Values()["game_enter"], 2);
    ASSERT_EQ(runtime.Logs()[1], "test.native.v2: [native] save_loaded");
    runtime.ResetState();
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
    EXPECT_TRUE(runtime.Diagnostics().empty());
    EXPECT_EQ(Values()["ticks"], 1);
    EXPECT_EQ(Values()["save_loaded"], 1);
}

TEST_F(ModNativeV2, EventFailureRollsBackItsStateAndWritesAndStopsLaterEvents) {
    LoadPlugin("fail_event");
    int commits = 0;
    runtime.Tick({1, 0, 0, true, 0, 1}, {}, {}, {}, [&](const auto &, int, std::string &) { ++commits; return true; });
    EXPECT_EQ(commits, 0);
    EXPECT_EQ(Values(), (Json{
                            {"mode", "fail_event"}
    }));
    EXPECT_FALSE(Json::parse(runtime.SaveState())["mods"]["test.native.v2"].contains("last_map"));
    EXPECT_FALSE(runtime.HasGameplay());
    ASSERT_FALSE(runtime.Diagnostics().empty());
    EXPECT_EQ(runtime.Logs().back(), "test.native.v2: [native] v2 stopped outside transaction");
}

TEST_F(ModNativeV2, TickFailureOrRejectedCommitRollsBackTickButKeepsEarlierEvents) {
    for (const bool fail_tick : {true, false}) {
        {
            mods::Manager empty;
            runtime.Start(empty);
        }
        LoadPlugin(fail_tick ? "fail_tick" : "");
        int commits = 0;
        runtime.Tick({1, 0, 0, true}, {}, {}, {}, [&](const auto &, int, std::string &) { ++commits; return false; });
        EXPECT_EQ(commits, fail_tick ? 0 : 1);
        EXPECT_EQ(Values()["game_enter"], 1);
        EXPECT_EQ(Values()["map_enter"], 1);
        EXPECT_FALSE(Values().contains("ticks"));
        EXPECT_FALSE(Values().contains("stopped"));
        EXPECT_FALSE(runtime.HasGameplay());
        ASSERT_FALSE(runtime.Diagnostics().empty());
    }
}

TEST_F(ModNativeV2, ThrowingCommitRollsBackOwnedStateAndStopsPlugin) {
    LoadPlugin();
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, [](const auto &, int, std::string &) -> bool {
        throw std::runtime_error("commit provider failed");
    });
    EXPECT_EQ(Values()["game_enter"], 1);
    EXPECT_FALSE(Values().contains("ticks"));
    EXPECT_FALSE(runtime.HasGameplay());
    ASSERT_FALSE(runtime.Diagnostics().empty());
    EXPECT_NE(runtime.Diagnostics().back().find("commit provider failed"), std::string::npos);
    EXPECT_EQ(runtime.Logs().back(), "test.native.v2: [native] v2 stopped outside transaction");
}

TEST_F(ModNativeV2, ScalarValidationAndSharedHostCallBudgetAreEnforced) {
    LoadPlugin("invalid_values");
    runtime.Tick({1, 0, 0, true});
    EXPECT_TRUE(runtime.Diagnostics().empty());
    EXPECT_EQ(Values()["valid"], "snowman \xe2\x98\x83");
    EXPECT_FALSE(Values().contains("bad"));
    {
        mods::Manager empty;
        runtime.Start(empty);
    }
    LoadPlugin("host_limit");
    runtime.Tick({1, 0, 0, true});
    EXPECT_TRUE(runtime.Diagnostics().empty());
    EXPECT_EQ(runtime.Logs().back(), "test.native.v2: [native] v2 shared host budget passed");
}

TEST_F(ModNativeV2, FullStateAllowsReplacementAndDeletionButNotNewKeys) {
    LoadPlugin("key_limit");
    auto  saved = Json::parse(runtime.SaveState());
    auto &values = saved["mods"]["test.native.v2"]["values"];
    values["seed"] = 1;
    for (int i = 0; i < 126; ++i) {
        values["key" + std::to_string(i)] = i;
    }
    ASSERT_EQ(values.size(), 128u);
    std::string error;
    ASSERT_TRUE(runtime.LoadState(saved.dump(), error)) << error;
    runtime.Tick({1, 0, 0, true});
    EXPECT_TRUE(runtime.Diagnostics().empty());
    EXPECT_EQ(Values().size(), 128u);
    EXPECT_FALSE(Values().contains("seed"));
    EXPECT_EQ(Values()["new_key"], true);
}

TEST_F(ModNativeV2, NoEventMaskReceivesOnlyTicksAndInvalidV2NeverFallsBack) {
    LoadPlugin({}, CHRONICLE_NATIVE_V2_NO_EVENTS_FIXTURE);
    runtime.Tick({1, 0, 0, true, 0, 1}, {}, {}, {}, [](const auto &, int, std::string &) { return true; });
    EXPECT_TRUE(runtime.Diagnostics().empty());
    EXPECT_EQ(Values(), (Json{
                            {"ticks", 1}
    }));
    {
        mods::Manager empty;
        runtime.Start(empty);
    }
    LoadPlugin({}, CHRONICLE_NATIVE_V2_BAD_FIXTURE);
    EXPECT_FALSE(runtime.HasGameplay());
    ASSERT_FALSE(runtime.Diagnostics().empty());
    EXPECT_NE(runtime.Diagnostics()[0].find("incompatible native ABI 2 descriptor"), std::string::npos);
}

TEST_F(ModNativeV2, MissingTrustPreventsAllCallbacksAndStateAccess) {
    LoadPlugin({}, CHRONICLE_NATIVE_V2_FIXTURE, false, true);
    EXPECT_FALSE(runtime.HasGameplay());
    EXPECT_TRUE(runtime.Logs().empty());
    runtime.Tick({1, 0, 0, true});
    EXPECT_TRUE(runtime.SaveState().empty());
}

TEST_F(ModNativeV2, NativeMenuActionsQueueAndCommitWithoutLua) {
    LoadPlugin({}, CHRONICLE_NATIVE_V2_MENU_FIXTURE);
    ASSERT_TRUE(runtime.Diagnostics().empty());
    ASSERT_EQ(runtime.Actions("test.native.v2").size(), 1u);
    EXPECT_EQ(runtime.Actions("test.native.v2")[0].title, "Native {cross} action");
    EXPECT_FALSE(runtime.Actions("test.native.v2")[0].available);
    std::string error;
    EXPECT_FALSE(runtime.QueueAction("test.native.v2", "run", error));
    int        money = 0;
    const auto commit = [&](const std::vector<mods::GameCommand> &commands, int, std::string &) {
        for (const auto &c : commands) {
            money += c.arguments.at("amount").get<int>();
        }
        return true;
    };
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
    ASSERT_TRUE(runtime.QueueAction("test.native.v2", "run", error));
    EXPECT_FALSE(runtime.QueueAction("test.native.v2", "run", error));
    EXPECT_FALSE(Values().contains("actions"));
    EXPECT_EQ(money, 7);
    runtime.Tick({1, 0, 7, true}, {}, {}, {}, commit);
    EXPECT_TRUE(runtime.Diagnostics().empty());
    EXPECT_EQ(Values()["actions"], 1);
    EXPECT_EQ(money, 19);
    EXPECT_EQ(runtime.Actions("test.native.v2")[0].result, "Completed.");
    runtime.Tick({1, 0, 19, true}, {}, {}, {}, commit);
    EXPECT_EQ(Values()["actions"], 1);
}

TEST_F(ModNativeV2, NativeMenuFailureRollsBackStateAndWritesAndUnloads) {
    for (const int failure : {0, 1, 2}) {
        LoadPlugin(failure == 0 ? "fail_action" : "", CHRONICLE_NATIVE_V2_MENU_FIXTURE);
        runtime.Tick({1, 0, 0, true}, {}, {}, {}, [](const auto &, int, std::string &) { return true; });
        std::string error;
        ASSERT_TRUE(runtime.QueueAction("test.native.v2", "run", error));
        int commits = 0;
        runtime.Tick({1, 0, 0, true}, {}, {}, {}, [&](const auto &, int, std::string &) -> bool {
            ++commits;
            if (failure == 2) {
                throw std::runtime_error("action commit failed");
            }
            return false;
        });
        EXPECT_EQ(commits, failure == 0 ? 0 : 1);
        EXPECT_FALSE(Values().contains("actions"));
        EXPECT_EQ(Values()["ticks"], 1);
        EXPECT_FALSE(runtime.HasGameplay());
        EXPECT_FALSE(runtime.Actions("test.native.v2")[0].available);
        EXPECT_EQ(runtime.Actions("test.native.v2")[0].result, "Failed. See module diagnostics.");
        EXPECT_EQ(runtime.Logs().back(), "test.native.v2: [native] v2 stopped outside transaction");
        EXPECT_FALSE(runtime.QueueAction("test.native.v2", "run", error));
    }
}

TEST_F(ModNativeV2, NativeMenuQueueIsCanceledByWorldChangesLoadResetAndUnload) {
    const auto  commit = [](const auto &, int, std::string &) { return true; };
    std::string error;
    for (int cancel = 0; cancel < 4; ++cancel) {
        LoadPlugin({}, CHRONICLE_NATIVE_V2_MENU_FIXTURE);
        runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
        ASSERT_TRUE(runtime.QueueAction("test.native.v2", "run", error));
        if (cancel == 0) {
            runtime.Tick({1, 1, 0, true}, {}, {}, {}, commit);
            EXPECT_TRUE(runtime.Actions("test.native.v2")[0].result.starts_with("Canceled:"));
        } else if (cancel == 1) {
            ASSERT_TRUE(runtime.LoadState(runtime.SaveState(), error)) << error;
        } else if (cancel == 2) {
            runtime.ResetState();
        } else {
            mods::Manager empty;
            runtime.Start(empty);
            EXPECT_TRUE(runtime.Actions("test.native.v2").empty());
            EXPECT_FALSE(runtime.QueueAction("test.native.v2", "run", error));
            continue;
        }
        runtime.Tick({1, 1, 0, true}, {}, {}, {}, commit);
        EXPECT_FALSE(Values().contains("actions"));
        EXPECT_TRUE(runtime.Diagnostics().empty());
    }
}

TEST_F(ModNativeV2, NativeAndLuaActionsShareNamespaceLimitsAndOwnedState) {
    const auto commit = [](const auto &, int, std::string &) { return true; };
    LoadPlugin({}, CHRONICLE_NATIVE_V2_MENU_FIXTURE, true, true,
               "assert(chronicle.action('lua','Lua action','Read native state',function() chronicle.set('seen',chronicle.get('actions')) end))");
    ASSERT_EQ(runtime.Actions("test.native.v2").size(), 2u);
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
    std::string error;
    ASSERT_TRUE(runtime.QueueAction("test.native.v2", "run", error));
    ASSERT_TRUE(runtime.QueueAction("test.native.v2", "lua", error));
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
    EXPECT_TRUE(runtime.Diagnostics().empty());
    EXPECT_EQ(Values()["seen"], 1);
    LoadPlugin({}, CHRONICLE_NATIVE_V2_MENU_FIXTURE, true, true,
               "assert(chronicle.action('run','Collision','Duplicate native ID',function() end))");
    EXPECT_FALSE(runtime.HasGameplay());
    ASSERT_FALSE(runtime.Diagnostics().empty());
    EXPECT_NE(runtime.Diagnostics().back().find("duplicate native action"), std::string::npos);
    LoadPlugin({}, CHRONICLE_NATIVE_V2_MENU_FIXTURE, true, true,
               "for i=1,32 do assert(chronicle.action('lua'..i,'Lua','Fill the action budget',function() end)) end");
    EXPECT_FALSE(runtime.HasGameplay());
    EXPECT_EQ(runtime.Actions("test.native.v2").size(), 32u);
    EXPECT_FALSE(runtime.Actions("test.native.v2")[0].available);
}

TEST_F(ModNativeV2, NativeMenuRejectsInvalidLabelsAndDoesNotLoadUntrustedPackages) {
    LoadPlugin({}, CHRONICLE_NATIVE_V2_MENU_BAD_FIXTURE);
    EXPECT_FALSE(runtime.HasGameplay());
    EXPECT_TRUE(runtime.Actions("test.native.v2").empty());
    ASSERT_FALSE(runtime.Diagnostics().empty());
    EXPECT_EQ(runtime.Logs().back(), "test.native.v2: [native] v2 stopped outside transaction");
    LoadPlugin({}, CHRONICLE_NATIVE_V2_MENU_FIXTURE, false, true);
    EXPECT_FALSE(runtime.HasGameplay());
    EXPECT_TRUE(runtime.Actions("test.native.v2").empty());
    EXPECT_TRUE(runtime.Logs().empty());
    LoadPlugin({}, CHRONICLE_NATIVE_V2_MENU_FIXTURE, true, false);
    EXPECT_FALSE(runtime.HasGameplay());
    EXPECT_TRUE(runtime.Actions("test.native.v2").empty());
    EXPECT_TRUE(runtime.Logs().empty());
}

TEST_F(ModNativeV2, DependencyFailureCancelsNativeActionAndUnloadsPluginImmediately) {
    LoadPlugin({}, CHRONICLE_NATIVE_V2_MENU_FIXTURE);
    auto provider = Manifest("test.provider");
    Add("local ticks=0; return {on_tick=function() ticks=ticks+1; if ticks==2 then error('dependency failed') end end}", provider);
    Json manifest;
    {
        std::ifstream input(root / "mods/test.native.v2/mod.json");
        input >> manifest;
    }
    manifest["requires"] = {"test.provider"};
    Write(root / "mods/test.native.v2/mod.json", manifest.dump());
    mods::Session preview;
    preview.Start({root / "mods"}, {}, root / "save");
    ASSERT_TRUE(preview.SetEnabled("test.provider", true));
    mods::Session active;
    active.Start({root / "mods"}, {}, root / "save");
    runtime.Start(active.Active());
    const auto commit = [](const auto &, int, std::string &) { return true; };
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
    ASSERT_TRUE(runtime.Diagnostics().empty());
    std::string error;
    ASSERT_TRUE(runtime.QueueAction("test.native.v2", "run", error));
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
    EXPECT_FALSE(Values().contains("actions"));
    EXPECT_FALSE(runtime.Actions("test.native.v2")[0].available);
    EXPECT_TRUE(runtime.Actions("test.native.v2")[0].result.starts_with("Canceled:"));
    EXPECT_EQ(runtime.Logs().back(), "test.native.v2: [native] v2 stopped outside transaction");
}

} // namespace
