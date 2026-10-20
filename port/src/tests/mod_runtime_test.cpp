#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "mods/api.hpp"
#include "audio/mixer.hpp"
#include "mod_audio_decode_fixture.hpp"
#include "mods/retail_sound.hpp"
#include "audio_retail_fixture.hpp"
#include "data_fixture.hpp"
#include "sound.hpp"
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

TEST_F(ModRuntime, EpochChangesOnlyWhenRuntimeStateIsReplaced) {
    Add("return {}");
    const auto initial = runtime.Epoch();
    Start();
    const auto started = runtime.Epoch();
    EXPECT_NE(initial, started);
    runtime.Tick({1, 0, 0, true});
    EXPECT_EQ(started, runtime.Epoch());
    std::string error;
    EXPECT_FALSE(runtime.LoadState("invalid", error));
    EXPECT_EQ(started, runtime.Epoch());
    ASSERT_TRUE(runtime.LoadState(runtime.SaveState(), error)) << error;
    const auto loaded = runtime.Epoch();
    EXPECT_NE(started, loaded);
    runtime.ResetState();
    EXPECT_NE(loaded, runtime.Epoch());
}

TEST_F(ModRuntime, QueuedWritesCarryTrustedOwnerAndFailedModulesLoseOwnership) {
    auto manifest = Manifest();
    manifest["permissions"] = {"economy.write"};
    Add(R"(local ticks=0
        return {on_tick=function()
            ticks=ticks+1
            if ticks>1 then error('stop owner') end
            assert(chronicle.call('economy.add',{amount=1}))
        end})",
        manifest);
    Start();
    ASSERT_EQ(runtime.ActiveOwners(), (std::vector<std::string>{"test.mod"}));
    unsigned   writes = 0;
    const auto commit = [&](const std::vector<mods::GameCommand> &commands, int, std::string &) {
        for (const auto &command : commands) {
            ++writes;
            EXPECT_EQ(command.owner, "test.mod");
            EXPECT_FALSE(command.arguments.contains("owner"));
        }
        return true;
    };
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
    EXPECT_EQ(writes, 1);
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
    EXPECT_TRUE(runtime.ActiveOwners().empty());
    EXPECT_EQ(writes, 1);
}

TEST_F(ModRuntime, EnemyDeathListenersReceiveDetachedContextBeforeTickAndRejectStaleFloors) {
    Add(R"(assert(chronicle.subscribe('enemy_killed',function(w)
        assert(w.event.entity=='16' and w.event.source=='scripted')
        assert(w.event.damage==5 and w.floor==1)
        chronicle.set('kills',(chronicle.get('kills') or 0)+1)
        w.event.damage=999
    end))
    return {on_tick=function(w) assert(w.event==nil) end})");
    Start();
    ASSERT_TRUE(runtime.WantsEnemyDeaths());
    const mods::World                     world{2, 0, 0, true, 0, 1};
    const std::array<mods::EnemyDeath, 1> deaths{{{16, 0, 1, -1, 10, 20, 5, true}}};
    runtime.Tick(world);
    runtime.Tick(world, {}, {}, {}, {}, deaths);
    EXPECT_EQ(State()["values"]["kills"], 1);
    EXPECT_EQ(deaths[0].damage, 5);
    runtime.Tick({2, 0, 0, true, 0, 2}, {}, {}, {}, {}, deaths);
    EXPECT_EQ(State()["values"]["kills"], 1);
    std::string error;
    ASSERT_TRUE(runtime.LoadState(runtime.SaveState(), error));
    runtime.Tick(world, {}, {}, {}, {}, deaths);
    EXPECT_EQ(State()["values"]["kills"], 1);
    EXPECT_TRUE(runtime.Diagnostics().empty());
}

TEST_F(ModRuntime, FailedEnemyListenerRollsBackStateAndWritesAndStopsSubscription) {
    auto manifest = Manifest();
    manifest["permissions"] = {"economy.write"};
    Add(R"(assert(chronicle.subscribe('enemy_killed',function(w)
        assert(chronicle.set('reward',true))
        assert(chronicle.add_money(2))
        error('reject reward')
    end)) return {})",
        manifest);
    Start();
    ASSERT_TRUE(runtime.Diagnostics().empty());
    ASSERT_TRUE(runtime.WantsEnemyDeaths());
    const mods::World world{2, 0, 10, true, 0, 1};
    runtime.Tick(world);
    int                                   commits = 0;
    const std::array<mods::EnemyDeath, 1> deaths{{{16, 0, 1}}};
    runtime.Tick(world, [&](int) { ++commits; return true; }, {}, {}, {}, deaths);
    EXPECT_EQ(commits, 0);
    EXPECT_FALSE(State()["values"].contains("reward"));
    EXPECT_FALSE(runtime.WantsEnemyDeaths());
    EXPECT_FALSE(runtime.Diagnostics().empty());
}

TEST_F(ModRuntime, ReturnedEnemyCallbackAlsoRequestsCaptureWithoutDoubleDelivery) {
    Add("return {on_enemy_killed=function(w) chronicle.set('kills',(chronicle.get('kills') or 0)+1) end}");
    Start();
    ASSERT_TRUE(runtime.WantsEnemyDeaths());
    const mods::World                     world{2, 0, 0, true, 0, 1};
    const std::array<mods::EnemyDeath, 1> deaths{{{16, 0, 1}}};
    runtime.Tick(world);
    runtime.Tick(world, {}, {}, {}, {}, deaths);
    EXPECT_EQ(State()["values"]["kills"], 1);
    EXPECT_TRUE(runtime.Diagnostics().empty());
}

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
    manifest["permissions"] = {"native.extension", "game.read", "game.write", "economy.write"};
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
    void LoadPlugin(const std::string &mode = {}, const fs::path &source = CHRONICLE_NATIVE_V2_FIXTURE, bool trust = true, bool enable = true, const std::string &script = {}, const std::vector<std::string> &dependencies = {}) {
        mods::Manager empty;
        runtime.Start(empty);
        runtime.ResetState();
        auto manifest = Manifest("test.native.v2");
        manifest.erase("quests");
        manifest["permissions"] = {"native.extension", "game.read", "game.write", "economy.write"};
        manifest["requires_capabilities"] = {"plugins.native.v2"};
        if (mode == "audio" || mode == "audio_fail") {
            manifest["permissions"].push_back("resources.read");
            manifest["audio"] = {{"tone", "tone.flac"}};
            Write(root / "mods/test.native.v2/tone.flac", std::string(reinterpret_cast<const char *>(kAudioFlac), sizeof(kAudioFlac)));
        }
        manifest["requires"] = dependencies;
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
        const auto found = std::ranges::find(preview.Preview().Packages(), "test.native.v2", &mods::Package::id);
        ASSERT_NE(found, preview.Preview().Packages().end());
        ASSERT_TRUE(preview.SetTrusted("test.native.v2", found->native_fingerprint, trust));
        for (const auto &id : dependencies) {
            ASSERT_TRUE(preview.SetEnabled(id, true));
        }
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

TEST_F(ModNativeV2, NativeHostWritesCarryRuntimePackageOwner) {
    LoadPlugin();
    unsigned writes = 0;
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, [&](const std::vector<mods::GameCommand> &commands, int, std::string &) {
        for (const auto &command : commands) {
            ++writes;
            EXPECT_EQ(command.owner, "test.native.v2");
        }
        return true;
    });
    EXPECT_GT(writes, 0);
    EXPECT_EQ(runtime.ActiveOwners(), (std::vector<std::string>{"test.native.v2"}));
    mods::Manager empty;
    runtime.Start(empty);
    EXPECT_TRUE(runtime.ActiveOwners().empty());
}

TEST_F(ModNativeV2, EnemyDeathsDeliverNativePayloadAndRollbackFailedCallbacks) {
    const mods::World                     world{2, 0, 0, true, 0, 1};
    const std::array<mods::EnemyDeath, 1> deaths{{{16, 0, 1, -1, 10, 20, 5, true}}};
    const auto                            commit = [](const auto &, int, auto &) { return true; };
    LoadPlugin();
    ASSERT_TRUE(runtime.WantsEnemyDeaths());
    runtime.Tick(world, {}, {}, {}, commit);
    runtime.Tick(world, {}, {}, {}, commit, deaths);
    ASSERT_TRUE(runtime.Diagnostics().empty());
    EXPECT_EQ(Values()["enemy_killed"], 1);
    LoadPlugin("fail_kill");
    runtime.Tick(world, {}, {}, {}, commit);
    int commits = 0;
    runtime.Tick(world, {}, {}, {}, [&](const auto &, int, auto &) { ++commits; return true; }, deaths);
    EXPECT_EQ(commits, 0);
    EXPECT_FALSE(Values().contains("enemy_killed"));
    EXPECT_FALSE(runtime.WantsEnemyDeaths());
    EXPECT_FALSE(runtime.Diagnostics().empty());
    LoadPlugin({}, CHRONICLE_NATIVE_V2_NO_EVENTS_FIXTURE);
    EXPECT_FALSE(runtime.WantsEnemyDeaths());
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

TEST_F(ModRuntime, CustomEventsAreDeferredDependencyScopedOrderedAndDetached) {
    auto provider = Manifest("source-mod");
    provider["permissions"] = {"game.write"};
    provider["requires_capabilities"] = {"events.custom.v1"};
    Add(R"(local sent=false
        assert(chronicle.subscribe_custom('source-mod','ready',function(w)
            assert(w.event.owner=='source-mod')
            assert(chronicle.set('self', (chronicle.get('self') or 0)+1))
        end))
        return {on_tick=function()
            if sent then return end
            sent=true
            for n=1,2 do
                local payload={value=n}
                assert(chronicle.call('events.emit',{name='ready',payload=payload}))
                payload.value=99
            end
        end})",
        provider);
    auto consumer = Manifest();
    consumer["requires"] = {"source-mod"};
    Add(R"(assert(chronicle.subscribe_custom('source-mod','ready',function(w)
            assert(w.event.owner=='source-mod' and w.event.name=='ready')
            assert(chronicle.set('order',(chronicle.get('order') or '')..w.event.payload.value))
            w.event.payload.value=999
        end))
        assert(chronicle.subscribe_custom('source-mod','ready',function(w)
            assert(w.event.payload.value~=999)
            assert(chronicle.set('order',chronicle.get('order')..w.event.payload.value))
        end))
        return {})",
        consumer);
    Add(R"(assert(not chronicle.subscribe_custom('source-mod','ready',function() error('leak') end)); return {})", Manifest("unrelated.mod"));
    Start();
    runtime.Tick({1, 0, 0, true});
    ASSERT_TRUE(runtime.Diagnostics().empty());
    EXPECT_FALSE(State().contains("values"));
    runtime.Tick({1, 0, 0, true});
    ASSERT_TRUE(runtime.Diagnostics().empty());
    EXPECT_EQ(State()["values"]["order"], "1122");
    EXPECT_EQ(Json::parse(runtime.SaveState())["mods"]["source-mod"]["values"]["self"], 2);
    runtime.Tick({1, 0, 0, true});
    EXPECT_EQ(State()["values"]["order"], "1122");
}

TEST_F(ModRuntime, CustomEventValidationAndQueryReadOnlyRules) {
    auto provider = Manifest("source.mod");
    provider["permissions"] = {"game.write"};
    Add(R"(assert(not chronicle.call('events.emit',{name='ready',payload={}}))
        assert(not chronicle.subscribe_custom('source.mod','bad:name',function() end))
        assert(chronicle.export_query('probe',function()
            assert(not chronicle.call('events.emit',{name='ready',payload={}}))
            return {ok=true}
        end))
        return {on_tick=function()
            assert(not chronicle.subscribe_custom('source.mod','late',function() end))
            assert(not chronicle.call('events.emit',{name='ready',payload={},owner='spoof.mod'}))
            assert(not chronicle.call('events.emit',{name='Bad',payload={}}))
            assert(not chronicle.call('events.emit',{name='bad\0name',payload={}}))
            assert(not chronicle.call('events.emit',{name='ready',payload={1,2}}))
            assert(not chronicle.call('events.emit',{name='ready',payload={x=0/0}}))
            assert(not chronicle.call('events.emit',{name='ready',payload={x=string.rep('x',16384)}}))
            for n=1,16 do assert(chronicle.call('events.emit',{name='ready',payload={n=n}})) end
            assert(not chronicle.call('events.emit',{name='ready',payload={}}))
        end})",
        provider);
    auto consumer = Manifest();
    consumer["requires"] = {"source.mod"};
    Add(R"(return {on_tick=function()
        assert(not chronicle.call('events.emit',{name='denied',payload={}}))
        assert(chronicle.invoke_query('source.mod','probe',{}).ok)
    end})",
        consumer);
    Start();
    runtime.Tick({1, 0, 0, true});
    runtime.Tick({1, 0, 0, true});
    EXPECT_TRUE(runtime.Diagnostics().empty());
}

TEST_F(ModRuntime, CustomEventQueueOverflowRejectsBeforeAnyGameCommit) {
    for (int i = 0; i < 5; ++i) {
        const std::string id = "source." + std::to_string(i);
        auto              manifest = Manifest(id);
        manifest["permissions"] = {"game.write", "economy.write"};
        Add("local sent=false; assert(chronicle.subscribe_custom('" + id + R"(','ready',function()
            assert(chronicle.set('seen',(chronicle.get('seen') or 0)+1))
        end)); return {on_tick=function()
            if sent then return end; sent=true
            assert(chronicle.set('sent',true))
            for n=1,16 do assert(chronicle.call('events.emit',{name='ready',payload={n=n}})) end
            assert(chronicle.call('economy.add',{amount=1}))
        end})",
            manifest);
    }
    Start();
    int  commits = 0;
    auto commit = [&](const auto &commands, int, auto &) {++commits;EXPECT_EQ(commands.size(),1);EXPECT_EQ(commands[0].operation,"economy.add");return true; };
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
    EXPECT_EQ(commits, 4);
    ASSERT_EQ(runtime.Diagnostics().size(), 1);
    EXPECT_NE(runtime.Diagnostics()[0].find("custom event queue capacity"), std::string::npos);
    auto states = Json::parse(runtime.SaveState())["mods"];
    EXPECT_FALSE(states["source.4"].contains("values"));
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
    states = Json::parse(runtime.SaveState())["mods"];
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(states["source." + std::to_string(i)]["values"]["seen"], 16);
    }
}

TEST_F(ModRuntime, CustomListenerFailureRollsBackOnlyItsPackage) {
    auto source = Manifest("source.mod");
    source["permissions"] = {"game.write"};
    Add(R"(local sent=false; return {on_tick=function()
        if sent then return end; sent=true
        assert(chronicle.call('events.emit',{name='ready',payload={}}))
    end})",
        source);
    auto consumer = Manifest();
    consumer["requires"] = {"source.mod"};
    consumer["permissions"] = {"game.write", "economy.write"};
    Add(R"(assert(chronicle.subscribe_custom('source.mod','ready',function()
        assert(chronicle.set('changed',true))
        assert(chronicle.call('events.emit',{name='leaked',payload={}}))
        assert(chronicle.call('economy.add',{amount=7}))
        error('listener failed')
    end)); return {})",
        consumer);
    auto sibling = Manifest("sibling.mod");
    sibling["requires"] = {"source.mod"};
    Add(R"(assert(chronicle.subscribe_custom('source.mod','ready',function()
        assert(chronicle.set('seen',true))
    end)); return {})",
        sibling);
    Start();
    int  commits = 0;
    auto commit = [&](const auto &, int, auto &) {++commits;return true; };
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
    EXPECT_EQ(commits, 0);
    EXPECT_FALSE(State().contains("values"));
    EXPECT_TRUE(Json::parse(runtime.SaveState())["mods"]["sibling.mod"]["values"]["seen"]);
    EXPECT_EQ(runtime.ActiveOwners(), (std::vector<std::string>{"source.mod", "sibling.mod"}));
}

TEST_F(ModRuntime, CustomEmissionFailureRollsBackStateAndDoesNotNotify) {
    for (bool fail_script : {true, false}) {
        auto source = Manifest();
        source["permissions"] = {"game.write", "economy.write"};
        Add(std::string(R"(assert(chronicle.subscribe_custom('test.mod','ready',function() error('leaked') end))
          return {on_tick=function()
            assert(chronicle.set('sent',true))
            assert(chronicle.call('events.emit',{name='ready',payload={}}))
            assert(chronicle.call('economy.add',{amount=7}))
        )") + (fail_script ? "error('emitter failed')" : "") +
                " end}",
            source);
        Start();
        int commits = 0;
        runtime.Tick({1, 0, 0, true}, {}, {}, {}, [&](const auto &, int, auto &) {++commits;return false; });
        runtime.Tick({1, 0, 0, true});
        EXPECT_EQ(commits, fail_script ? 0 : 1);
        EXPECT_FALSE(State().contains("values"));
        ASSERT_EQ(runtime.Diagnostics().size(), 1);
        EXPECT_EQ(runtime.Diagnostics()[0].find("leaked"), std::string::npos);
    }
}

TEST_F(ModRuntime, CustomEventRecursionIsDeferredAndContextChangesDiscardQueues) {
    auto manifest = Manifest();
    manifest["permissions"] = {"game.write"};
    Add(R"(local sent=false
        assert(chronicle.subscribe_custom('test.mod','again',function()
            assert(chronicle.set('seen',(chronicle.get('seen') or 0)+1))
            assert(chronicle.call('events.emit',{name='again',payload={}}))
        end))
        return {on_tick=function()
            if sent then return end; sent=true
            assert(chronicle.call('events.emit',{name='again',payload={}}))
        end})",
        manifest);
    Start();
    const mods::World world{2, 0, 0, true, 0, 1};
    runtime.Tick(world);
    EXPECT_FALSE(State().contains("values"));
    runtime.Tick(world);
    EXPECT_EQ(State()["values"]["seen"], 1);
    runtime.Tick(world);
    EXPECT_EQ(State()["values"]["seen"], 2);
    runtime.Tick({2, 0, 0, true, 0, 2});
    runtime.Tick({2, 0, 0, true, 0, 2});
    EXPECT_EQ(State()["values"]["seen"], 2);
    std::string error;
    ASSERT_TRUE(runtime.LoadState(runtime.SaveState(), error));
    runtime.Tick(world);
    EXPECT_EQ(State()["values"]["seen"], 2);
    runtime.Tick(world);
    EXPECT_EQ(State()["values"]["seen"], 3);
    runtime.Tick({2, 0, 0, false, 0, 1});
    runtime.Tick(world);
    runtime.Tick(world);
    EXPECT_EQ(State()["values"]["seen"], 3);
    runtime.ResetState();
    runtime.Tick(world);
    EXPECT_FALSE(State().contains("values"));
    runtime.Tick(world);
    EXPECT_EQ(State()["values"]["seen"], 1);
    ASSERT_TRUE(runtime.LoadState(runtime.SaveState(), error));
    runtime.Tick(world);
    EXPECT_EQ(State()["values"]["seen"], 1);
    runtime.Tick({2, 1, 0, true, 0, 1});
    runtime.Tick({2, 1, 0, true, 0, 1});
    EXPECT_EQ(State()["values"]["seen"], 1);
    EXPECT_TRUE(runtime.Diagnostics().empty());
}

TEST_F(ModNativeV2, CustomEventsUseTheSameNativeAndLuaQueueAndDetachedPayload) {
    LoadPlugin("emit_custom", CHRONICLE_NATIVE_V2_FIXTURE, true, true, R"(
        assert(chronicle.subscribe_custom('test.native.v2','pulse',function(w)
            assert(w.event.owner=='test.native.v2' and w.event.payload.answer==42)
            assert(chronicle.set('lua_seen',true))
            w.event.payload.answer=100
        end))
    )");
    runtime.Tick({1, 0, 0, true});
    EXPECT_TRUE(Values()["emitted"]);
    EXPECT_FALSE(Values().contains("custom"));
    runtime.Tick({1, 0, 0, true});
    EXPECT_TRUE(Values()["lua_seen"]);
    EXPECT_EQ(Values()["custom"], 1);
    EXPECT_TRUE(runtime.Diagnostics().empty());
    runtime.Tick({1, 0, 0, true});
    EXPECT_EQ(Values()["custom"], 1);
    LoadPlugin({}, CHRONICLE_NATIVE_V2_FIXTURE, true, true, R"(
        local sent=false
        assert(chronicle.subscribe('tick',function()
            if sent then return end; sent=true
            assert(chronicle.call('events.emit',{name='pulse',payload={answer=42}}))
        end))
    )");
    const auto commit = [](const auto &, int, auto &) { return true; };
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
    EXPECT_EQ(Values()["custom"], 1);
    EXPECT_TRUE(runtime.Diagnostics().empty());
}

TEST_F(ModNativeV2, CustomNativeFailureAndOptOutDoNotLeakManagedChanges) {
    LoadPlugin("fail_custom");
    runtime.Tick({1, 0, 0, true});
    int commits = 0;
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, [&](const auto &, int, auto &) {++commits;return true; });
    EXPECT_EQ(commits, 0);
    EXPECT_FALSE(Values().contains("custom"));
    EXPECT_TRUE(Values()["emitted"]);
    EXPECT_FALSE(runtime.HasGameplay());
    LoadPlugin("fail_emit_custom");
    runtime.Tick({1, 0, 0, true});
    runtime.Tick({1, 0, 0, true});
    EXPECT_FALSE(Values().contains("emitted"));
    EXPECT_FALSE(Values().contains("custom"));
    EXPECT_FALSE(runtime.HasGameplay());
    LoadPlugin("emit_custom", CHRONICLE_NATIVE_V2_NO_EVENTS_FIXTURE);
    runtime.Tick({1, 0, 0, true});
    runtime.Tick({1, 0, 0, true});
    EXPECT_TRUE(Values()["emitted"]);
    EXPECT_FALSE(Values().contains("custom"));
    EXPECT_TRUE(runtime.Diagnostics().empty());
}

TEST_F(ModRuntime, DependencyCommandsCommitOrderedWritesWithProviderIdentityAndOwnedEvents) {
    auto provider = Manifest("test.provider");
    provider["permissions"] = {"game.write", "economy.write"};
    provider["requires_capabilities"] = {"services.command.v1"};
    provider["quests"][0].erase("map");
    Add(R"(assert(chronicle.subscribe_custom('test.provider','changed',function(w)
        assert(w.event.owner=='test.provider' and w.event.payload.value==5)
        assert(chronicle.set('event_seen',true))
    end))
    assert(chronicle.export_command('reward',function(a,caller)
        assert(caller=='test.mod')
        assert(chronicle.set('value',a.value))
        assert(chronicle.quest_add('visit',1)==1)
        assert(chronicle.add_money(2))
        assert(chronicle.call('economy.add',{amount=9}))
        assert(chronicle.call('events.emit',{name='changed',payload={value=a.value}}))
        a.value=100
        return {value=chronicle.get('value')}
    end)); return {})",
        provider);
    auto consumer = Manifest();
    consumer["requires"] = {"test.provider"};
    consumer["permissions"] = {"game.write", "economy.write"};
    Add(R"(local once=false; return {on_tick=function()
        if once then return end;once=true
        assert(chronicle.call('economy.add',{amount=1}))
        local a={value=5}
        local r=assert(chronicle.call('services.command',{provider='test.provider',name='reward',arguments=a}))
        assert(a.value==5 and r.value==5)
        r.value=200
        assert(chronicle.call('economy.add',{amount=3}))
        assert(chronicle.set('done',true))
    end})",
        consumer);
    Start();
    int commits = 0;
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, [&](const std::vector<mods::GameCommand> &commands, int money, auto &) {
        ++commits;
        EXPECT_EQ(money, 2);
        EXPECT_EQ(commands.size(), 3);
        if (commands.size() == 3) {
            EXPECT_EQ(commands[0].owner, "test.mod");
            EXPECT_EQ(commands[0].arguments["amount"], 1);
            EXPECT_EQ(commands[1].owner, "test.provider");
            EXPECT_EQ(commands[1].arguments["amount"], 9);
            EXPECT_EQ(commands[2].owner, "test.mod");
            EXPECT_EQ(commands[2].arguments["amount"], 3);
        }
        return true;
    });
    EXPECT_EQ(commits, 1);
    ASSERT_TRUE(runtime.Diagnostics().empty()) << Json(runtime.Diagnostics()).dump();
    EXPECT_TRUE(State()["values"]["done"]);
    auto state = Json::parse(runtime.SaveState())["mods"]["test.provider"];
    EXPECT_EQ(state["values"]["value"], 5);
    EXPECT_EQ(state["quests"]["visit"], 1);
    EXPECT_FALSE(state["values"].contains("event_seen"));
    runtime.Tick({1, 0, 0, true});
    EXPECT_TRUE(Json::parse(runtime.SaveState())["mods"]["test.provider"]["values"]["event_seen"]);
}

TEST_F(ModRuntime, DependencyCommandRollbackCoversEveryProviderAndPreservesPriorCallbacks) {
    for (int failure = 0; failure < 3; ++failure) {
        auto base = Manifest("test.base");
        base["permissions"] = {"game.write", "economy.write"};
        Add(R"(assert(chronicle.set('ticks',0))
          assert(chronicle.export_command('change',function(a,caller)
            assert(caller=='test.middle')
            assert(chronicle.set('changed',true))
            assert(chronicle.call('economy.add',{amount=7}))
            assert(chronicle.call('events.emit',{name='changed',payload={}}))
            return {}
          end))
          assert(chronicle.subscribe_custom('test.base','changed',function() error('event leaked') end))
          return {on_tick=function() assert(chronicle.set('ticks',chronicle.get('ticks')+1)) end})",
            base);
        auto middle = Manifest("test.middle");
        middle["requires"] = {"test.base"};
        middle["permissions"] = {"game.write"};
        Add(R"(assert(chronicle.export_command('change',function()
            assert(chronicle.set('changed',true))
            return assert(chronicle.call('services.command',{provider='test.base',name='change',arguments={}}))
        end)); return {})",
            middle);
        auto consumer = Manifest();
        consumer["requires"] = {"test.middle"};
        consumer["permissions"] = {"game.write"};
        Add(std::string(R"(return {on_tick=function()
            assert(chronicle.set('changed',true))
            assert(chronicle.call('services.command',{provider='test.middle',name='change',arguments={}}))
        )") + (failure == 0 ? "error('consumer failed')" : "") +
                " end}",
            consumer);
        Start();
        int commits = 0;
        runtime.Tick({1, 0, 0, true}, {}, {}, {}, [&](const auto &, int, auto &) -> bool {++commits;if(failure==2){throw std::runtime_error("commit failed");
}return false; });
        EXPECT_EQ(commits, failure == 0 ? 0 : 1);
        auto states = Json::parse(runtime.SaveState())["mods"];
        EXPECT_EQ(states["test.base"]["values"]["ticks"], 1);
        EXPECT_FALSE(states["test.base"]["values"].contains("changed"));
        EXPECT_FALSE(states["test.middle"].contains("values"));
        EXPECT_FALSE(State().contains("values"));
        runtime.Tick({1, 0, 0, true});
        EXPECT_EQ(Json::parse(runtime.SaveState())["mods"]["test.base"]["values"]["ticks"], 2);
        ASSERT_EQ(runtime.Diagnostics().size(), 1);
        EXPECT_EQ(runtime.Diagnostics()[0].find("event leaked"), std::string::npos);
    }
}

TEST_F(ModRuntime, FailedCommandPoisonsCallerEvenWhenHandledAndProviderRemainsUsable) {
    auto provider = Manifest("test.provider");
    provider["permissions"] = {"economy.write"};
    Add(R"(assert(chronicle.export_command('bad',function()
        assert(chronicle.set('value',99))
        assert(chronicle.call('economy.add',{amount=99}))
        error('bad provider')
    end))
    assert(chronicle.export_command('good',function()
        assert(not chronicle.get('value'))
        assert(chronicle.set('value',7))
        assert(chronicle.call('economy.add',{amount=7}))
        return {value=7}
    end));return {})",
        provider);
    auto consumer = Manifest();
    consumer["requires"] = {"test.provider"};
    consumer["permissions"] = {"game.write"};
    Add(R"(return {on_tick=function()
        local value,error=chronicle.call('services.command',{provider='test.provider',name='bad',arguments={}})
        assert(not value and error:find('bad provider'))
        assert(chronicle.set('ignored_failure',true))
    end})",
        consumer);
    auto other = Manifest("z.consumer");
    other["requires"] = {"test.provider"};
    other["permissions"] = {"game.write"};
    Add(R"(return {on_tick=function()
        assert(chronicle.call('services.command',{provider='test.provider',name='good',arguments={}}).value==7)
    end})",
        other);
    Start();
    int commits = 0;
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, [&](const auto &commands, int, auto &) {++commits;EXPECT_EQ(commands.size(),1);EXPECT_EQ(commands[0].arguments["amount"],7);return true; });
    EXPECT_EQ(commits, 1);
    EXPECT_FALSE(State().contains("values"));
    EXPECT_EQ(Json::parse(runtime.SaveState())["mods"]["test.provider"]["values"]["value"], 7);
    ASSERT_EQ(runtime.Diagnostics().size(), 1);
    EXPECT_EQ(runtime.ActiveOwners(), (std::vector<std::string>{"test.provider", "z.consumer"}));
}

TEST_F(ModRuntime, CommandRegistrationAuthorizationAndReadOnlyQueriesStayBounded) {
    auto base = Manifest("test.base");
    base["permissions"] = {"game.write"};
    Add(R"(assert(chronicle.export_command('change',function() error('unauthorized command') end));return {})", base);
    auto query = Manifest("test.query");
    query["requires"] = {"test.base"};
    query["permissions"] = {"game.write"};
    Add(R"(assert(chronicle.export_query('probe',function()
        local r,e=chronicle.call('services.command',{provider='test.base',name='change',arguments={}})
        assert(not r and e:find('writable'))
        return {ok=true}
    end));return {})",
        query);
    auto consumer = Manifest();
    consumer["requires"] = {"test.query"};
    consumer["permissions"] = {"game.write"};
    Add(R"(assert(not chronicle.export_command('bad:name',function() end))
        for i=1,16 do
            assert(chronicle.export_command('name'..i,function() return {} end))
            assert(chronicle.export_query('name'..i,function() return {} end))
        end
        assert(not chronicle.export_command('extra',function() end))
        assert(not chronicle.export_query('extra',function() end))
        assert(not chronicle.call('services.command',{provider='test.query',name='probe',arguments={}}))
        return {on_tick=function()
            assert(not chronicle.export_command('late',function() end))
            assert(chronicle.invoke_query('test.query','probe',{}).ok)
            local r,e=chronicle.call('services.command',{provider='test.base',name='change',arguments={}})
            assert(not r and e:find('declared dependency'))
            assert(not chronicle.call('services.command',{provider='test.query',name='bad\0name',arguments={}}))
            assert(not chronicle.call('services.command',{provider='test.query',name='probe',arguments={},caller='spoof'}))
        end})",
        consumer);
    auto denied = Manifest("z.denied");
    denied["requires"] = {"test.base"};
    Add(R"(return {on_tick=function()
        local r,e=chronicle.call('services.command',{provider='test.base',name='change',arguments={}})
        assert(not r and e:find('permission'))
    end})",
        denied);
    Start();
    runtime.Tick({1, 0, 0, true});
    EXPECT_TRUE(runtime.Diagnostics().empty());
}

TEST_F(ModRuntime, CommandWritesEventsAndLegacyMoneyShareRootLimits) {
    auto provider = Manifest("test.provider");
    provider["permissions"] = {"game.write", "economy.write"};
    Add(R"(assert(chronicle.export_command('bulk',function()
        for i=1,32 do assert(chronicle.call('economy.add',{amount=1})) end
        for i=1,8 do assert(chronicle.call('events.emit',{name='bulk',payload={}})) end
        return {money=not not chronicle.add_money(20000)}
    end));return {})",
        provider);
    auto consumer = Manifest();
    consumer["requires"] = {"test.provider"};
    consumer["permissions"] = {"game.write", "economy.write"};
    Add(R"(return {on_tick=function()
        assert(chronicle.call('services.command',{provider='test.provider',name='bulk',arguments={}}).money)
        assert(not chronicle.call('services.command',{provider='test.provider',name='bulk',arguments={}}).money)
        assert(not chronicle.call('economy.add',{amount=1}))
        assert(not chronicle.call('events.emit',{name='overflow',payload={}}))
    end})",
        consumer);
    Start();
    int commits = 0;
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, [&](const auto &commands, int money, auto &) {++commits;EXPECT_EQ(commands.size(),64);EXPECT_EQ(money,20000);return true; });
    EXPECT_EQ(commits, 1);
    EXPECT_TRUE(runtime.Diagnostics().empty());
}

TEST_F(ModRuntime, QueryAndCommandInvocationsShareBudgetAndCommandFailureRollsBack) {
    auto provider = Manifest("test.provider");
    Add(R"(assert(chronicle.export_query('read',function() return {} end))
        assert(chronicle.export_command('change',function()
            assert(chronicle.set('count',(chronicle.get('count') or 0)+1));return {}
        end));return {})",
        provider);
    auto consumer = Manifest();
    consumer["requires"] = {"test.provider"};
    consumer["permissions"] = {"game.write"};
    Add(R"(return {on_tick=function()
        assert(chronicle.invoke_query('test.provider','read',{}))
        for i=1,15 do assert(chronicle.call('services.command',{provider='test.provider',name='change',arguments={}})) end
        local r,e=chronicle.call('services.command',{provider='test.provider',name='change',arguments={}})
        assert(not r and e:find('limit'))
        assert(chronicle.set('ignored_limit',true))
    end})",
        consumer);
    Start();
    runtime.Tick({1, 0, 0, true});
    EXPECT_FALSE(State().contains("values"));
    EXPECT_FALSE(Json::parse(runtime.SaveState())["mods"]["test.provider"].contains("values"));
    ASSERT_EQ(runtime.Diagnostics().size(), 1);
}

TEST_F(ModRuntime, InvalidCommandResultsCannotCommitProviderChanges) {
    for (const std::string body : {"return {1,2}", "return function() end", "return {x=0/0}", "while true do end"}) {
        auto provider = Manifest("test.provider");
        Add("assert(chronicle.export_command('bad',function() assert(chronicle.set('changed',true)); " + body + " end)); return {}", provider);
        auto consumer = Manifest();
        consumer["requires"] = {"test.provider"};
        consumer["permissions"] = {"game.write"};
        Add(R"(return {on_tick=function()
            local r,e=chronicle.call('services.command',{provider='test.provider',name='bad',arguments={}})
            assert(not r and e)
        end})",
            consumer);
        Start();
        runtime.Tick({1, 0, 0, true});
        EXPECT_FALSE(Json::parse(runtime.SaveState())["mods"]["test.provider"].contains("values"));
        EXPECT_EQ(runtime.Diagnostics().size(), 1);
    }
}

TEST_F(ModNativeV2, NativeDependencyCommandsCommitAndRollbackProviderAndCallerTogether) {
    for (const auto &mode : {std::string("command"), std::string("fail_command")}) {
        auto provider = Manifest("test.provider");
        provider["permissions"] = {"game.read", "game.write", "economy.write"};
        Add(R"(assert(chronicle.export_command('reward',function(a,caller)
            assert(caller=='test.native.v2')
            assert(chronicle.player().hp==30)
            assert(chronicle.set('changed',true))
            assert(chronicle.call('economy.add',{amount=a.value}))
            assert(chronicle.add_money(2))
            return {value=a.value+100}
        end)); return {})",
            provider);
        LoadPlugin(mode, CHRONICLE_NATIVE_V2_FIXTURE, true, true, {}, {"test.provider"});
        int commits = 0;
        runtime.Tick({1, 0, 0, true}, {}, [](auto) { return Json{
                                                         {"hp", 30}
 }; }, {}, [&](const auto &commands, int money, auto &) {
            ++commits;EXPECT_EQ(commands.size(),1);EXPECT_EQ(commands[0].owner,"test.provider");EXPECT_EQ(commands[0].arguments["amount"],5);EXPECT_EQ(money,2);return true; });
        const auto provider_state = Json::parse(runtime.SaveState())["mods"]["test.provider"];
        if (mode == "command") {
            EXPECT_EQ(commits, 1);
            EXPECT_EQ(Values()["command_calls"], 1);
            EXPECT_TRUE(provider_state["values"]["changed"]);
            EXPECT_TRUE(runtime.Diagnostics().empty());
        } else {
            EXPECT_EQ(commits, 0);
            EXPECT_FALSE(Values().contains("command_calls"));
            EXPECT_FALSE(provider_state.contains("values"));
            ASSERT_EQ(runtime.Diagnostics().size(), 1);
        }
    }
}

TEST_F(ModRuntime, CommandDepthLimitAndMissingExportsPoisonTheOuterTransaction) {
    for (int i = 8; i >= 0; --i) {
        auto manifest = Manifest("chain." + std::to_string(i));
        manifest["permissions"] = {"game.write"};
        const auto next = "chain." + std::to_string(i + 1);
        if (i < 8) {
            manifest["requires"] = {next};
        }
        const std::string body = i == 8 ? "return {}" : "return assert(chronicle.call('services.command',{provider='" + next + "',name='next',arguments={}}))";
        Add("assert(chronicle.export_command('next',function() assert(chronicle.set('visited',true)); " + body + " end));return {}", manifest);
    }
    auto consumer = Manifest();
    consumer["permissions"] = {"game.write"};
    consumer["requires"] = {"chain.0"};
    Add(R"(return {on_tick=function()
        assert(not chronicle.call('services.command',{provider='chain.0',name='next',arguments={}}))
        assert(chronicle.set('ignored',true))
    end})",
        consumer);
    Start();
    runtime.Tick({1, 0, 0, true});
    ASSERT_EQ(runtime.Diagnostics().size(), 1);
    auto states = Json::parse(runtime.SaveState())["mods"];
    for (int i = 0; i <= 8; ++i) {
        EXPECT_FALSE(states["chain." + std::to_string(i)].contains("values"));
    }
    EXPECT_FALSE(State().contains("values"));
    Add(R"(return {on_tick=function()
        assert(not chronicle.call('services.command',{provider='chain.0',name='missing',arguments={}}))
        assert(chronicle.set('ignored',true))
    end})",
        consumer);
    Start();
    runtime.Tick({1, 0, 0, true});
    ASSERT_EQ(runtime.Diagnostics().size(), 1);
    EXPECT_FALSE(State().contains("values"));
}

TEST_F(ModRuntime, CommandExportsRebindAfterSaveLoadAndReset) {
    auto provider = Manifest("test.provider");
    Add(R"(assert(chronicle.export_command('add',function()
        local n=(chronicle.get('count') or 0)+1
        assert(chronicle.set('count',n));return {count=n}
    end));return {})",
        provider);
    auto consumer = Manifest();
    consumer["permissions"] = {"game.write"};
    consumer["requires"] = {"test.provider"};
    Add(R"(return {on_tick=function()
        local r=assert(chronicle.call('services.command',{provider='test.provider',name='add',arguments={}}))
        assert(chronicle.set('last',r.count))
    end})",
        consumer);
    Start();
    runtime.Tick({1, 0, 0, true});
    EXPECT_EQ(State()["values"]["last"], 1);
    std::string error;
    ASSERT_TRUE(runtime.LoadState(runtime.SaveState(), error));
    runtime.Tick({1, 0, 0, true});
    EXPECT_EQ(State()["values"]["last"], 2);
    runtime.ResetState();
    runtime.Tick({1, 0, 0, true});
    EXPECT_EQ(State()["values"]["last"], 1);
    EXPECT_TRUE(runtime.Diagnostics().empty());
}

class ModNativeServices : public ModRuntime {
protected:
    void LoadServices(const fs::path    &source = CHRONICLE_NATIVE_SERVICES_FIXTURE,
                      const std::string &script = {}, const std::vector<std::string> &dependencies = {}, bool trust = true) {
        mods::Manager empty;
        runtime.Start(empty);
        runtime.ResetState();
        auto manifest = Manifest("test.native.services");
        manifest.erase("quests");
        manifest["permissions"] = {"native.extension", "game.read", "game.write", "economy.write"};
        manifest["requires_capabilities"] = {"services.native.v1"};
        manifest["requires"] = dependencies;
        manifest["native_library"] = "plugin" + source.extension().string();
        const auto package = root / "mods/test.native.services";
        if (script.empty()) {
            manifest.erase("script");
        } else {
            Write(package / "main.lua", script);
        }
        Write(package / "mod.json", manifest.dump());
        fs::copy_file(source, package / manifest["native_library"].get<std::string>(), fs::copy_options::overwrite_existing);
        mods::Session preview;
        preview.Start({root / "mods"}, {}, root / "save");
        const auto native = std::ranges::find(preview.Preview().Packages(), "test.native.services", &mods::Package::id);
        ASSERT_NE(native, preview.Preview().Packages().end());
        ASSERT_TRUE(preview.SetTrusted("test.native.services", native->native_fingerprint, trust));
        const auto packages = preview.Preview().Packages();
        for (const auto &p : packages) {
            ASSERT_TRUE(preview.SetEnabled(p.id, true));
        }
        mods::Session active;
        active.Start({root / "mods"}, {}, root / "save");
        runtime.Start(active.Active());
    }

    void Consumer(const std::string &script) {
        auto m = Manifest();
        m["requires"] = {"test.native.services"};
        m["permissions"] = {"game.read", "game.write", "economy.write"};
        Add(script, m);
    }

    Json Provider() { return Json::parse(runtime.SaveState())["mods"]["test.native.services"]; }

    void Run(const mods::CommitGame &commit = {}) {
        runtime.Tick({1, 0, 0, true}, {}, {}, [](auto operation, const auto &) {
            EXPECT_EQ(operation,"economy.get");return Json{{"money",40}}; }, commit);
    }

    void TearDown() override {
        mods::Manager empty;
        runtime.Start(empty);
        ModRuntime::TearDown();
    }
};

TEST_F(ModNativeServices, LuaQueriesAndCommandsUseNativeExportsAndTrustedOwnership) {
    Consumer(R"(local once=false
        assert(chronicle.subscribe_custom('test.native.services','changed',function(w)
            assert(w.event.owner=='test.native.services')
            assert(chronicle.set('event_seen',true))
        end))
        return {on_tick=function()
            if once then return end;once=true
            local first=assert(chronicle.invoke_query('test.native.services','inspect',{}))
            assert(first.value==0 and first.caller=='test.mod')
            assert(chronicle.call('economy.add',{amount=1}))
            local r=assert(chronicle.call('services.command',{provider='test.native.services',name='change',arguments={}}))
            assert(r.value==9 and r.caller=='test.mod')
            local q=assert(chronicle.call('services.query',{provider='test.native.services',name='inspect',arguments={}}))
            assert(q.value==9 and q.caller=='test.mod')
            assert(chronicle.call('economy.add',{amount=2}))
            assert(chronicle.set('done',true))
        end})");
    LoadServices();
    ASSERT_TRUE(runtime.Diagnostics().empty()) << Json(runtime.Diagnostics()).dump();
    int commits = 0;
    Run([&](const auto &commands, int money, auto &) {
        ++commits;
        EXPECT_EQ(money, 0);
        EXPECT_EQ(commands.size(), 3);
        if (commands.size() == 3) {
            EXPECT_EQ(commands[1].owner, "test.native.services");
            EXPECT_EQ(commands[1].arguments["amount"], 7);
        }
        return true;
    });
    ASSERT_TRUE(runtime.Diagnostics().empty()) << Json(runtime.Diagnostics()).dump();
    EXPECT_EQ(commits, 1);
    EXPECT_EQ(Provider()["values"]["value"], 9);
    EXPECT_TRUE(State()["values"]["done"]);
    Run();
    EXPECT_TRUE(State()["values"]["event_seen"]);
}

TEST_F(ModNativeServices, QueryFailuresAndInvalidResultsAreRecoverableAndNeverWriteState) {
    Consumer(R"(return {on_tick=function()
        for _,kind in ipairs({'bad_array','bad_json','bad_utf8','null_result','oversize','fail'}) do
            local r,e=chronicle.call('services.query',{provider='test.native.services',name='inspect',arguments={kind=kind}})
            assert(not r and e)
        end
        assert(chronicle.invoke_query('test.native.services','inspect',{}).value==0)
        assert(chronicle.set('ticks',(chronicle.get('ticks') or 0)+1))
    end})");
    LoadServices();
    Run();
    Run();
    EXPECT_TRUE(runtime.Diagnostics().empty()) << Json(runtime.Diagnostics()).dump();
    EXPECT_FALSE(Provider().contains("values"));
    EXPECT_EQ(State()["values"]["ticks"], 2);
}

TEST_F(ModNativeServices, CommandAndConsumerFailuresRollbackNativeOwnedStateWritesAndNotifications) {
    for (int failure = 0; failure < 4; ++failure) {
        Consumer(std::string(R"(return {on_tick=function()
            local r,e=chronicle.call('services.command',{provider='test.native.services',name='change',arguments={kind=')") +
                 (failure == 0 ? "fail" : failure == 1 ? "bad_array"
                                                       : "ok") +
                 R"('}})
            assert(chronicle.set('changed',true))
        )" + (failure == 2 ? "error('consumer failed')" : "") +
                 " end}");
        auto observer = Manifest("z.observer");
        observer["requires"] = {"test.native.services"};
        Add(R"(assert(chronicle.subscribe_custom('test.native.services','changed',function() error('event leaked') end));return {})", observer);
        LoadServices();
        int commits = 0;
        Run([&](const auto &, int, auto &) {++commits;return false; });
        EXPECT_EQ(commits, failure == 3 ? 1 : 0);
        EXPECT_FALSE(Provider().contains("values"));
        EXPECT_FALSE(State().contains("values"));
        Run();
        ASSERT_EQ(runtime.Diagnostics().size(), 1) << Json(runtime.Diagnostics()).dump();
        EXPECT_EQ(runtime.Diagnostics()[0].find("event leaked"), std::string::npos);
    }
}

TEST_F(ModNativeServices, NativeExportsRelayToLuaQueriesAndCommandsWithSharedIdentityAndBudgets) {
    auto base = Manifest("test.base");
    base["permissions"] = {"economy.write"};
    Add(R"(assert(chronicle.export_query('read',function() return {value=22} end))
        assert(chronicle.export_command('change',function(a,caller)
            assert(caller=='test.native.services')
            assert(chronicle.set('changed',true))
            assert(chronicle.call('economy.add',{amount=4}))
            return {value=23}
        end));return {})",
        base);
    Consumer(R"(local once=false;return {on_tick=function()
        if once then return end;once=true
        assert(chronicle.invoke_query('test.native.services','relay',{}).value==22)
        assert(chronicle.call('services.command',{provider='test.native.services',name='relay',arguments={}}).value==23)
        local budget=assert(chronicle.call('services.query',{provider='test.native.services',name='budget',arguments={}}))
        assert(budget.calls==11)
        local r,e=chronicle.invoke_query('test.native.services','inspect',{})
        assert(not r and e:find('limit'))
    end})");
    LoadServices(CHRONICLE_NATIVE_SERVICES_FIXTURE, {}, {"test.base"});
    int commits = 0;
    Run([&](const auto &commands, int, auto &) {++commits;EXPECT_EQ(commands.size(),1);EXPECT_EQ(commands[0].owner,"test.base");return true; });
    EXPECT_EQ(commits, 1);
    EXPECT_TRUE(runtime.Diagnostics().empty()) << Json(runtime.Diagnostics()).dump();
    EXPECT_TRUE(Json::parse(runtime.SaveState())["mods"]["test.base"]["values"]["changed"]);
}

TEST_F(ModNativeServices, InvalidDescriptorsAndLuaCollisionsRejectThePackage) {
    for (const auto &source : {fs::path(CHRONICLE_NATIVE_SERVICES_BAD_FIXTURE), fs::path(CHRONICLE_NATIVE_SERVICES_BAD_KIND_FIXTURE)}) {
        LoadServices(source);
        EXPECT_FALSE(runtime.HasGameplay());
        ASSERT_FALSE(runtime.Diagnostics().empty());
    }
    LoadServices(CHRONICLE_NATIVE_SERVICES_FIXTURE, R"(assert(chronicle.export_query('inspect',function() return {} end));return {})");
    EXPECT_FALSE(runtime.HasGameplay());
    ASSERT_FALSE(runtime.Diagnostics().empty());
    EXPECT_NE(runtime.Diagnostics()[0].find("duplicate native service"), std::string::npos);
    LoadServices(CHRONICLE_NATIVE_SERVICES_FIXTURE, R"(for i=1,28 do assert(chronicle.export_query('q'..i,function() return {} end)) end;return {})");
    EXPECT_FALSE(runtime.HasGameplay());
    ASSERT_FALSE(runtime.Diagnostics().empty());
    LoadServices(CHRONICLE_NATIVE_SERVICES_FIXTURE, R"(for i=1,27 do assert(chronicle.export_query('q'..i,function() return {} end)) end;return {})");
    EXPECT_TRUE(runtime.HasGameplay());
    EXPECT_TRUE(runtime.Diagnostics().empty());
}

TEST_F(ModNativeServices, NativeServicesExpireOnUnloadAndRebindAfterSaveLoadOrReset) {
    Consumer(R"(return {on_tick=function()
        local r=assert(chronicle.invoke_query('test.native.services','inspect',{}))
        assert(chronicle.set('seen',r.value))
    end})");
    LoadServices();
    Run();
    EXPECT_EQ(State()["values"]["seen"], 0);
    std::string error;
    ASSERT_TRUE(runtime.LoadState(runtime.SaveState(), error));
    Run();
    EXPECT_EQ(State()["values"]["seen"], 0);
    runtime.ResetState();
    Run();
    EXPECT_EQ(State()["values"]["seen"], 0);
    EXPECT_TRUE(runtime.Diagnostics().empty());
    LoadServices(CHRONICLE_NATIVE_SERVICES_FIXTURE, {}, {}, false);
    EXPECT_FALSE(runtime.HasGameplay());
    Run();
    EXPECT_TRUE(runtime.SaveState().empty());
}

TEST_F(ModRuntime, AudioMenuActionsRenderStopAndRetireAtSceneChange) {
    auto manifest = Manifest();
    manifest["audio"] = {{"tone", "tone.flac"}};
    manifest["permissions"] = {"resources.read", "game.read", "game.write"};
    Add(R"(
        assert(chronicle.action('play','Play tone','Play owned tone',function()
            assert(chronicle.call('audio.play',{id='tone',loop=true,volume=0.5}))
        end))
        assert(chronicle.action('stop','Stop tone','Stop owned tone',function()
            local state=assert(chronicle.call('audio.state'))
            assert(#state.sounds==1 and #state.voices==1)
            assert(chronicle.call('audio.stop',{handle=state.voices[1].handle}))
        end))
        return {}
    )", manifest);
    Write(root / "mods/test.mod/tone.flac", std::string(reinterpret_cast<const char *>(kAudioFlac), sizeof(kAudioFlac)));
    Start();
    ASSERT_TRUE(runtime.Diagnostics().empty());
    auto render = [] {
        std::array<float, 64> samples{};
        audio::DefaultMixer().Render(samples.data(), 32);
        return samples[0];
    };
    audio::DefaultMixer().Reset();
    runtime.Tick({1, 0, 0, true});
    std::string error;
    ASSERT_TRUE(runtime.QueueAction("test.mod", "play", error));
    EXPECT_EQ(render(), 0); // no eager side effect while queued
    runtime.Tick({1, 0, 0, true});
    ASSERT_TRUE(runtime.Diagnostics().empty());
    EXPECT_NEAR(render(), 0.125f, 0.001f);
    ASSERT_TRUE(runtime.QueueAction("test.mod", "stop", error));
    runtime.Tick({1, 0, 0, true});
    EXPECT_EQ(render(), 0);
    ASSERT_TRUE(runtime.QueueAction("test.mod", "play", error));
    runtime.Tick({1, 0, 0, true});
    EXPECT_NEAR(render(), 0.125f, 0.001f);
    runtime.Tick({1, 1, 0, true});
    EXPECT_EQ(render(), 0);
}
TEST_F(ModRuntime, FailedAudioCallbackNeverPublishesPlaybackOrOwnedState) {
    auto manifest = Manifest();
    manifest["audio"] = {{"tone", "tone.flac"}};
    manifest["permissions"] = {"resources.read", "game.read", "game.write"};
    Add(R"(return {on_tick=function()
        chronicle.set('changed',true)
        assert(chronicle.call('audio.play',{id='tone',loop=true}))
        error('cancel callback')
    end})", manifest);
    Write(root / "mods/test.mod/tone.flac", std::string(reinterpret_cast<const char *>(kAudioFlac), sizeof(kAudioFlac)));
    Start();
    audio::DefaultMixer().Reset();
    runtime.Tick({1, 0, 0, true});
    EXPECT_FALSE(runtime.Diagnostics().empty());
    EXPECT_FALSE(State().value("values", Json::object()).contains("changed"));
    std::array<float, 64> samples{};
    audio::DefaultMixer().Render(samples.data(), 32);
    for (float sample : samples) EXPECT_EQ(sample, 0);
}

TEST_F(ModNativeV2, NativeAudioActionsRenderStopAndRollbackWithoutLuaPlayback) {
    auto render = [] {
        std::array<float, 64> samples{};
        audio::DefaultMixer().Render(samples.data(), 32);
        return samples[0];
    };
    const auto commit = [](const auto &, int, std::string &) { return true; };
    for (const std::string mode : {"audio", "audio_fail"}) {
        LoadPlugin(mode, CHRONICLE_NATIVE_V2_MENU_FIXTURE);
        ASSERT_TRUE(runtime.Diagnostics().empty()) << Json(runtime.Diagnostics()).dump();
        audio::DefaultMixer().Reset();
        runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
        std::string error;
        ASSERT_TRUE(runtime.QueueAction("test.native.v2", "run", error));
        EXPECT_EQ(render(), 0);
        runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
        if (mode == "audio_fail") {
            EXPECT_EQ(render(), 0);
            EXPECT_FALSE(Values().contains("audio_actions"));
            EXPECT_FALSE(runtime.HasGameplay());
            EXPECT_FALSE(runtime.Diagnostics().empty());
        } else {
            ASSERT_TRUE(runtime.Diagnostics().empty()) << Json(runtime.Diagnostics()).dump();
            EXPECT_NEAR(render(), 0.125f, 0.001f);
            EXPECT_EQ(Values()["audio_actions"], 1);
            ASSERT_TRUE(runtime.QueueAction("test.native.v2", "run", error));
            runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
            EXPECT_EQ(render(), 0);
            EXPECT_EQ(Values()["audio_actions"], 2);
            ASSERT_TRUE(runtime.QueueAction("test.native.v2", "run", error));
            runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
            EXPECT_NEAR(render(), 0.125f, 0.001f);
            runtime.Start(mods::Manager{});
            EXPECT_EQ(render(), 0);
        }
    }
}

TEST_F(ModRuntime, RetailSoundMenuActionCommitsThroughNativeDriverWithCustomAudioLoaded) {
    auto manifest = Manifest();
    manifest["permissions"] = {"resources.read", "game.write"};
    manifest["audio"] = {{"tone", "tone.flac"}};
    Add(R"(assert(chronicle.action('se','Play effect','Play loaded retail effect',function()
        assert(chronicle.call('audio.se_play',{se=1,volume=127}))
    end)); return {})", manifest);
    Write(root / "mods/test.mod/tone.flac", std::string(reinterpret_cast<const char *>(kAudioFlac), sizeof(kAudioFlac)));
    Start();
    ASSERT_TRUE(runtime.Diagnostics().empty());
    const auto hd = chronicle_audio::fixture::MakeHd({});
    const auto bd = chronicle_audio::fixture::SquareVag(2);
    const std::string list = "test_g.hd\n";
    auto pack = datafix::MakePack({{"sound.txt", {list.begin(), list.end()}}, {"test_g.hd", hd}, {"test_g.bd", bd}});
    CSound sound;
    auto &mixer = audio::DefaultMixer();
    sound.Step(); mixer.Reset();
    char name[] = "sound.txt";
    sound.LoadSoundFileFromPack(name, reinterpret_cast<unsigned int *>(pack.data()));
    mixer.SetVolume(10, audio::kFullPortVolume);
    const auto original = se_info[1];
    struct Restore { SND_SE_INFO row; ~Restore() { se_info[1] = row; } } restore{original};
    se_info[1] = {0, 0, 0, 10, -1}; // synthetic table fixture, never saved
    int commits = 0;
    const auto commit = [&](const auto &commands, int money, std::string &error) {
        ++commits;
        return mods::ApplyRetailEffect(commands, money, -1, -1, -1, error);
    };
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
    std::string error;
    ASSERT_TRUE(runtime.QueueAction("test.mod", "se", error));
    EXPECT_EQ(commits, 0);
    runtime.Tick({1, 0, 0, true}, {}, {}, {}, commit);
    ASSERT_TRUE(runtime.Diagnostics().empty()) << Json(runtime.Diagnostics()).dump();
    EXPECT_EQ(commits, 1); // custom AudioStreams must not intercept this domain
    EXPECT_EQ(mixer.ActiveVoices(), 0);
    sound.Step();
    std::array<float, 8192> output{};
    mixer.Render(output.data(), 4096);
    EXPECT_GT(mixer.ActiveVoices(), 0);
    EXPECT_TRUE(std::ranges::any_of(output, [](float sample) { return sample != 0; }));
    sound.SE_Stop(10, 0, 0, 0); sound.Step(); mixer.Reset();
}

TEST_F(ModRuntime, MusicReplacementActionCommitsAndClearsOwnedMapping) {
    auto manifest = Manifest();
    manifest["audio"] = {{"tone", "tone.flac"}};
    manifest["permissions"] = {"resources.read", "game.read", "game.write"};
    Add(R"(
        assert(chronicle.action('set','Replace music','Own sequence',function()
            assert(chronicle.call('audio.music_set',{sequence='test_a.sq',id='tone'}))
        end))
        assert(chronicle.action('clear','Restore music','Release sequence',function()
            assert(#chronicle.call('audio.state').music==1)
            assert(chronicle.call('audio.music_clear',{sequence='test_a.sq'}))
        end))
        return {}
    )", manifest);
    Write(root / "mods/test.mod/tone.flac", std::string(reinterpret_cast<const char *>(kAudioFlac), sizeof(kAudioFlac)));
    Start(); ASSERT_TRUE(runtime.Diagnostics().empty());
    runtime.Tick({1, 0, 0, true});
    std::string error;
    ASSERT_TRUE(runtime.QueueAction("test.mod", "set", error));
    EXPECT_FALSE(runtime.BindMusic(0, "test_a.sq", {}));
    runtime.Tick({1, 0, 0, true});
    ASSERT_TRUE(runtime.Diagnostics().empty());
    EXPECT_TRUE(runtime.BindMusic(0, "test_a.sq", {}));
    ASSERT_TRUE(runtime.QueueAction("test.mod", "clear", error));
    runtime.Tick({1, 0, 0, true});
    ASSERT_TRUE(runtime.Diagnostics().empty());
    EXPECT_FALSE(runtime.BindMusic(0, "test_a.sq", {}));
}
TEST_F(ModRuntime, FailedMusicActionDoesNotPublishReplacement) {
    auto manifest = Manifest(); manifest["audio"] = {{"tone", "tone.flac"}};
    manifest["permissions"] = {"resources.read", "game.write"};
    Add(R"(return {on_tick=function()
        assert(chronicle.call('audio.music_set',{sequence='test_a.sq',id='tone'}))
        error('cancel music change')
    end})", manifest);
    Write(root / "mods/test.mod/tone.flac", std::string(reinterpret_cast<const char *>(kAudioFlac), sizeof(kAudioFlac)));
    Start(); runtime.Tick({1, 0, 0, true});
    EXPECT_FALSE(runtime.Diagnostics().empty());
    EXPECT_FALSE(runtime.BindMusic(0, "test_a.sq", {}));
}
