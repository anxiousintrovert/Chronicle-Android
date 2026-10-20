#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "mods/runtime.hpp"
#include "mods/save.hpp"
#include "mods/save_migrations.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>

namespace {
using Json=nlohmann::json;
class ModSaveMigration:public testing::Test {
protected:
    std::filesystem::path root=std::filesystem::temp_directory_path()/("chronicle-migrate-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    mods::Manager manager;mods::Runtime runtime;std::map<std::string,bool> enabled;
    void TearDown()override{mods::Scripts().Start(mods::Manager{});mods::Scripts().ResetState();std::error_code ec;std::filesystem::remove_all(root,ec);}
    void Add(const std::string &id,const Json &rules,const std::string &script="return {}") {
        auto path=root/id;std::filesystem::create_directories(path);
        std::ofstream(path/"mod.json")<<Json{{"schema_version",1},{"id",id},{"name",id},{"version","2"},{"game","chronicle"},
            {"files",Json::object()},{"api_version",1},{"script","main.lua"},{"save_migrations",rules},{"requires_capabilities",{"save.migrations.v1"}}}.dump();
        std::ofstream(path/"main.lua")<<script;enabled[id]=true;
    }
    void Start(){manager.Scan({root},{},enabled);runtime.Start(manager);}
    Json Saved(Json state,const std::string &version="1"){return Json{{"schema_version",1},{"required",{{"test.mod",version}}},{"mods",{{"test.mod",state}}}};}
};
TEST_F(ModSaveMigration, DirectMigrationPreservesOwnedProgressAndPublishesCurrentVersion) {
    const Json rules={{{"from","1"},{"rename_values",{{"old","new"}}},{"rename_quests",{{"old.quest","new.quest"}}},
        {"remove_values",{"obsolete"}},{"defaults",{{"new",99},{"introduced",true}}}}};
    Add("test.mod",rules,"return {on_save_loaded=function() chronicle.set('loaded',true) end}");Start();
    ASSERT_TRUE(manager.Errors().empty());ASSERT_EQ(manager.LoadOrder().size(),1u);ASSERT_TRUE(runtime.Diagnostics().empty());
    const auto saved=Saved({{"values",{{"old",7},{"obsolete",false},{"reward_given",true}}},{"quests",{{"old.quest",5}}},{"last_map",3}});
    std::string error;ASSERT_TRUE(runtime.LoadState(saved.dump(),error))<<error;
    auto result=Json::parse(runtime.SaveState());EXPECT_EQ(result["required"]["test.mod"],"2");
    EXPECT_EQ(result["mods"]["test.mod"]["values"],(Json{{"new",7},{"introduced",true},{"reward_given",true}}));
    EXPECT_EQ(result["mods"]["test.mod"]["quests"],(Json{{"new.quest",5}}));
    EXPECT_EQ(result["mods"]["test.mod"]["last_map"],3);
    runtime.Tick({1,0,0,true});EXPECT_EQ(Json::parse(runtime.SaveState())["mods"]["test.mod"]["values"]["loaded"],true);
    ASSERT_TRUE(runtime.LoadState(runtime.SaveState(),error))<<error; // Exact version does not rerun migration.
    EXPECT_EQ(Json::parse(runtime.SaveState())["mods"]["test.mod"]["values"]["new"],7);
}
TEST_F(ModSaveMigration, ChecksummedSaveMigrationDoesNotRewriteSourceOrNativePrefix) {
    Add("test.mod",Json::array(),"return {on_tick=function() chronicle.set('old',42) end}");
    auto manifest_path=root/"test.mod/mod.json";Json manifest;
    {std::ifstream file(manifest_path);file>>manifest;}
    manifest["version"]="1";{std::ofstream file(manifest_path);file<<manifest.dump();}
    Start();mods::Scripts().Start(manager);mods::Scripts().Tick({1,0,0,true});
    const std::string native("native\0prefix",13);auto path=root/"old.sav";
    ASSERT_EQ(mods::WriteSave(path,native.data(),native.size(),false),FilesResult::kWritten);
    const auto bytes=[&](const auto &file){std::ifstream input(file,std::ios::binary);return std::string(std::istreambuf_iterator<char>(input),{});};
    const auto original=bytes(path);ASSERT_EQ(original.substr(0,native.size()),native);
    Add("test.mod",Json{{{"from","1"},{"rename_values",{{"old","new"}}}}});Start();mods::Scripts().Start(manager);
    std::string error;ASSERT_TRUE(mods::LoadSaveState(path,native.size(),error))<<error;
    EXPECT_EQ(bytes(path),original);EXPECT_EQ(Json::parse(mods::Scripts().SaveState())["mods"]["test.mod"]["values"]["new"],42);
    auto migrated=root/"new.sav";ASSERT_EQ(mods::WriteSave(migrated,native.data(),native.size(),false),FilesResult::kWritten);
    EXPECT_EQ(bytes(migrated).substr(0,native.size()),native);
    ASSERT_TRUE(mods::LoadSaveState(migrated,native.size(),error))<<error;
    EXPECT_EQ(Json::parse(mods::Scripts().SaveState())["required"]["test.mod"],"2");
    mods::Scripts().Start(mods::Manager{});mods::Scripts().ResetState();auto vanilla=root/"vanilla.sav";
    ASSERT_EQ(mods::WriteSave(vanilla,native.data(),native.size(),false),FilesResult::kWritten);EXPECT_EQ(bytes(vanilla),native);
}
TEST_F(ModSaveMigration, CollisionOrSecondOwnerFailureLeavesEntireLiveStateAndEpochUntouched) {
    const Json rules={{{"from","1"},{"rename_values",{{"old","new"}}}}};
    Add("test.mod",rules);Add("test.second",rules);Start();
    const auto before=runtime.SaveState();const auto epoch=runtime.Epoch();
    auto saved=Saved({{"values",{{"old",4}}}});
    saved["required"]["test.second"]="1";saved["mods"]["test.second"]={{"values",{{"old",5},{"new",6}}}};
    std::string error;EXPECT_FALSE(runtime.LoadState(saved.dump(),error));EXPECT_NE(error.find("test.second"),std::string::npos);
    EXPECT_EQ(runtime.SaveState(),before);EXPECT_EQ(runtime.Epoch(),epoch);
}
TEST_F(ModSaveMigration, MissingMigrationDisabledAndFailedOwnersRemainIncompatible) {
    Add("test.mod",Json::array());Start();auto saved=Saved(Json::object());std::string error;
    EXPECT_FALSE(runtime.LoadState(saved.dump(),error));
    enabled["test.mod"]=false;Start();EXPECT_FALSE(runtime.LoadState(saved.dump(),error));
    Add("test.mod",Json::array(),"return {on_tick=function() error('failed owner') end}");Start();runtime.Tick({1,0,0,true});
    saved["required"]["test.mod"]="2";EXPECT_FALSE(runtime.LoadState(saved.dump(),error));
}
TEST_F(ModSaveMigration, InvalidSavedValuesCannotBeLaunderedByRemovalAndDefaultsRespectLimits) {
    Add("test.mod",Json{{{"from","1"},{"remove_values",{"bad"}},{"defaults",{{"extra",1}}}}});Start();
    const auto before=runtime.SaveState();std::string error;
    EXPECT_FALSE(runtime.LoadState(Saved({{"values",{{"bad",Json::array()}}}}).dump(),error));
    Json values=Json::object();for(int i=0;i<128;++i)values["v"+std::to_string(i)]=i;
    EXPECT_FALSE(runtime.LoadState(Saved({{"values",values}}).dump(),error));EXPECT_EQ(runtime.SaveState(),before);
}
TEST_F(ModSaveMigration, SimultaneousRenamesAllowSwapsWithoutOverwritingUnrelatedState) {
    Json rules={{{"from","1"},{"rename_values",{{"a","b"},{"b","a"}}}}};
    mods::ValidateSaveMigrations(rules,"2");Json state={{"values",{{"a",1},{"b",2},{"other",3}}}};
    ASSERT_TRUE(mods::MigrateSavedState(state,rules,"1"));EXPECT_EQ(state["values"],(Json{{"a",2},{"b",1},{"other",3}}));
    const auto before=state;EXPECT_FALSE(mods::MigrateSavedState(state,rules,"unknown"));EXPECT_EQ(state,before);
}
TEST_F(ModSaveMigration, InvalidDeclarationsBlockPackageDiscovery) {
    const std::vector<Json> rules={
        {{{"from","2"}}},{{{"from","1"}},{{"from","1"}}},{{{"from","1"},{"unknown",true}}},
        {{{"from","1"},{"rename_values",{{"a","same"},{"b","same"}}}}},
        {{{"from","1"},{"rename_values",{{"a","b"}}},{"remove_values",{"b"}}}},
        {{{"from","1"},{"defaults",{{"bad",Json::array()}}}}},
        {{{"from","1"},{"remove_values",{"x"}},{"defaults",{{"x",1}}}}}
    };
    for(const auto &invalid:rules){Add("test.mod",invalid);Start();EXPECT_TRUE(manager.LoadOrder().empty())<<invalid.dump();ASSERT_EQ(manager.Packages().size(),1u);EXPECT_FALSE(manager.Packages()[0].problems.empty());}
}
}
