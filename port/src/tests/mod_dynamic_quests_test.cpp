#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "mods/quests.hpp"
#include "mods/runtime.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
namespace {
using Json=nlohmann::json;
Json Definition(const std::string &id="dynamic") {return {{"id",id},{"title","Explore"},{"description","Find three treasures."},{"goal",3}};}
TEST(ModDynamicQuests, RegistrationIsIdempotentAndCannotReplaceOtherDefinitions) {
    mods::Package package;Json state=Json::object();const auto args=Definition();
    auto result=mods::CallOwnedQuest(package,state,"quests.register",args);EXPECT_EQ(result["progress"],0);
    const auto before=state;EXPECT_EQ(mods::CallOwnedQuest(package,state,"quests.register",args),result);EXPECT_EQ(state,before);
    auto changed=args;changed["goal"]=4;EXPECT_THROW(mods::CallOwnedQuest(package,state,"quests.register",changed),std::runtime_error);EXPECT_EQ(state,before);
    package.quests.push_back({"manifest","Title","Description",1,-1});
    EXPECT_THROW(mods::CallOwnedQuest(package,state,"quests.register",Definition("manifest")),std::runtime_error);EXPECT_EQ(state,before);
    result=mods::CallOwnedQuest(package,state,"quests.advance",{{"id","dynamic"},{"amount",1000000}});
    EXPECT_EQ(result["progress"],3);EXPECT_EQ(result["complete"],true);
}
TEST(ModDynamicQuests, SharedLimitsRejectWithoutPartialState) {
    mods::Package package;Json state=Json::object();
    for(int i=0;i<63;++i)package.quests.push_back({"q"+std::to_string(i),"Title","Description",1,-1});
    mods::CallOwnedQuest(package,state,"quests.register",Definition());const auto before=state;
    EXPECT_THROW(mods::CallOwnedQuest(package,state,"quests.register",Definition("extra")),std::runtime_error);EXPECT_EQ(state,before);
    for(int i=0;i<64;++i)state["quests"]["old"+std::to_string(i)]=1;
    const auto full=state;EXPECT_THROW(mods::CallOwnedQuest(package,state,"quests.advance",{{"id","dynamic"},{"amount",1}}),std::runtime_error);EXPECT_EQ(state,full);
}
TEST(ModDynamicQuests, StrictValidationRejectsMalformedDefinitionsAndArguments) {
    const auto good=Definition();
    for(const Json &changes:std::vector<Json>{{{"goal",true}},{{"goal",0}},{{"goal",1000001}},{{"id","Other.owner"}},{{"title",""}},{{"description","line\nbreak"}},{{"reward",42}}}) {
        auto bad=good;bad.update(changes);EXPECT_FALSE(mods::ValidateQuestOperation("quests.register",bad).empty());
    }
    for(const Json &amount:std::vector<Json>{-1,1000001,true,1.5})EXPECT_FALSE(mods::ValidateQuestOperation("quests.advance",{{"id","dynamic"},{"amount",amount}}).empty());
    Json state={{"quest_definitions",{{"dynamic",{{"title","Title"},{"description","Description"},{"goal",0}}}}}};
    EXPECT_THROW(mods::QuestDefinitions({},state),std::runtime_error);
}
Json MultiDefinition() {
    return {{"id","multi"},{"title","Rebuild the village"},{"description","Recover supplies and find the villagers."},
        {"objectives",{{{"id","supplies"},{"title","Recover supplies"},{"goal",2}},{{"id","villagers"},{"title","Find villagers"},{"goal",3}}}}};
}
TEST(ModDynamicQuests, NamedObjectivesAdvanceIndependentlyAndCompleteOnlyTogether) {
    mods::Package package;Json state=Json::object();auto result=mods::CallOwnedQuest(package,state,"quests.register",MultiDefinition());
    ASSERT_EQ(result["objectives"].size(),2u);EXPECT_EQ(result["goal"],5);EXPECT_EQ(result["progress"],0);
    const auto before=state;
    EXPECT_THROW(mods::CallOwnedQuest(package,state,"quests.advance",{{"id","multi"},{"amount",1}}),std::runtime_error);
    EXPECT_THROW(mods::CallOwnedQuest(package,state,"quests.advance",{{"id","multi"},{"objective","unknown"},{"amount",1}}),std::runtime_error);EXPECT_EQ(state,before);
    result=mods::CallOwnedQuest(package,state,"quests.advance",{{"id","multi"},{"objective","supplies"},{"amount",99}});
    EXPECT_EQ(result["progress"],2);EXPECT_EQ(result["complete"],false);EXPECT_EQ(result["objectives"][0]["progress"],2);EXPECT_EQ(result["objectives"][1]["progress"],0);
    result=mods::CallOwnedQuest(package,state,"quests.advance",{{"id","multi"},{"objective","villagers"},{"amount",3}});
    EXPECT_EQ(result["complete"],true);EXPECT_EQ(state["quests"]["multi"],5);
    EXPECT_EQ(mods::CallOwnedQuest(package,state,"quests.register",MultiDefinition()),result);
}
TEST(ModDynamicQuests, ObjectiveShapesLimitsAndCorruptCountsAreRejected) {
    auto definition=MultiDefinition();definition["objectives"]=Json::array();EXPECT_FALSE(mods::ValidateQuestOperation("quests.register",definition).empty());
    definition=MultiDefinition();definition["objectives"][1]["id"]="supplies";EXPECT_FALSE(mods::ValidateQuestOperation("quests.register",definition).empty());
    definition=MultiDefinition();definition["objectives"][1]["goal"]=1000000;EXPECT_FALSE(mods::ValidateQuestOperation("quests.register",definition).empty());
    definition=MultiDefinition();definition["goal"]=5;EXPECT_FALSE(mods::ValidateQuestOperation("quests.register",definition).empty());
    definition=MultiDefinition();definition["objectives"]=Json::array();for(int i=0;i<17;++i)definition["objectives"].push_back({{"id","o"+std::to_string(i)},{"title","Objective"},{"goal",1}});
    EXPECT_FALSE(mods::ValidateQuestOperation("quests.register",definition).empty());
    Json state=Json::object();mods::CallOwnedQuest({},state,"quests.register",MultiDefinition());
    for(const auto &counts:std::vector<Json>{{{"supplies",3}},{{"foreign",1}},{{"supplies",true}},{{"supplies",1}}}) {
        auto bad=state;bad["quest_objectives"]["multi"]=counts;EXPECT_THROW(mods::QuestDefinitions({},bad),std::runtime_error);
    }
    auto bad=state;bad["quests"]["multi"]=1;EXPECT_THROW(mods::QuestDefinitions({},bad),std::runtime_error);
    bad=state;bad["quest_objectives"]["other"]=Json::object();EXPECT_THROW(mods::QuestDefinitions({},bad),std::runtime_error);
}

class ModDynamicQuestRuntime:public testing::Test {
protected:
    std::filesystem::path root=std::filesystem::temp_directory_path()/("chronicle-dynamic-quest-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    mods::Manager manager;mods::Runtime runtime;std::map<std::string,bool> enabled;
    void TearDown()override{std::error_code error;std::filesystem::remove_all(root,error);}
    void Add(const std::string &id,const std::string &script,Json migrations=Json::array()) {
        auto path=root/id;std::filesystem::create_directories(path);
        std::ofstream(path/"mod.json")<<Json{{"schema_version",1},{"id",id},{"name",id},{"version","2"},{"game","chronicle"},
            {"files",Json::object()},{"api_version",1},{"script","main.lua"},{"permissions",{"game.read","game.write","economy.write"}},
            {"save_migrations",migrations},{"requires_capabilities",{"quests.dynamic.v1"}}}.dump();
        std::ofstream(path/"main.lua")<<script;enabled[id]=true;
    }
    void Start(){manager.Scan({root},{},enabled);runtime.Start(manager);}
    Json State(){return Json::parse(runtime.SaveState());}
    const std::string register_code="assert(chronicle.call('quests.register',{id='dynamic',title='Explore',description='Find treasures',goal=3})); ";
};
TEST_F(ModDynamicQuestRuntime, SavedDefinitionsRestoreAndLegacyHelpersSeeSameProgress) {
    Add("test.mod","return {on_tick=function() "+register_code+" assert(chronicle.quest_add('dynamic',1)); assert(chronicle.quest_get('dynamic')==1); end}");
    Start();runtime.Tick({1,2,0,true});ASSERT_TRUE(runtime.Diagnostics().empty());ASSERT_EQ(runtime.Quests().size(),1u);EXPECT_EQ(runtime.Quests()[0].progress,1);
    const auto saved=runtime.SaveState();runtime.ResetState();EXPECT_TRUE(runtime.Quests().empty());
    std::string error;ASSERT_TRUE(runtime.LoadState(saved,error))<<error;ASSERT_EQ(runtime.Quests().size(),1u);EXPECT_EQ(runtime.Quests()[0].progress,1);
    EXPECT_EQ(runtime.SaveState(),saved);
}
TEST_F(ModDynamicQuestRuntime, OwnersCanUseSameIdWithoutSharingProgressOrDefinitions) {
    for(const char *id:{"test.one","test.two"})Add(id,"return {on_tick=function() "+register_code+" assert(chronicle.call('quests.advance',{id='dynamic',amount="+(std::string(id)=="test.one"?"1":"2")+"})); end}");
    Start();runtime.Tick({1,2,0,true});ASSERT_TRUE(runtime.Diagnostics().empty());const auto state=State();
    EXPECT_EQ(state["mods"]["test.one"]["quests"]["dynamic"],1);EXPECT_EQ(state["mods"]["test.two"]["quests"]["dynamic"],2);
}
TEST_F(ModDynamicQuestRuntime, CallbackAndRejectedGameWritesRollBackDynamicDefinitionsTogether) {
    for(bool callback_error:{true,false}) {
        Add("test.mod","return {on_tick=function() "+register_code+" assert(chronicle.call('quests.advance',{id='dynamic',amount=2})); assert(chronicle.add_money(1)); "+(callback_error?"error('rollback')":"")+" end}");
        Start();runtime.ResetState();runtime.Tick({1,2,0,true},[](int){return false;});
        EXPECT_FALSE(runtime.Diagnostics().empty());EXPECT_FALSE(State()["mods"]["test.mod"].contains("quest_definitions"));EXPECT_FALSE(State()["mods"]["test.mod"].contains("quests"));
    }
}
TEST_F(ModDynamicQuestRuntime, CorruptSavedDefinitionRejectsLoadAndMigrationMovesDefinitionWithProgress) {
    Add("test.mod","return {on_tick=function() "+register_code+" chronicle.quest_add('dynamic',2) end}",Json{{{"from","1"},{"rename_quests",{{"dynamic","renamed"}}}}});
    Start();runtime.Tick({1,2,0,true});const auto saved=State();const auto before=runtime.SaveState();const auto epoch=runtime.Epoch();
    auto bad=saved;bad["mods"]["test.mod"]["quest_definitions"]["dynamic"]["goal"]=false;std::string error;
    EXPECT_FALSE(runtime.LoadState(bad.dump(),error));EXPECT_EQ(runtime.SaveState(),before);EXPECT_EQ(runtime.Epoch(),epoch);
    auto old=saved;old["required"]["test.mod"]="1";ASSERT_TRUE(runtime.LoadState(old.dump(),error))<<error;
    EXPECT_EQ(runtime.Quests()[0].id,"renamed");EXPECT_EQ(runtime.Quests()[0].progress,2);
    EXPECT_FALSE(State()["mods"]["test.mod"]["quest_definitions"].contains("dynamic"));
}
TEST_F(ModDynamicQuestRuntime, MultiObjectiveSaveMigrationKeepsEveryCounterAndMenuDetail) {
    Add("test.mod",R"(return {on_tick=function()
        assert(chronicle.call('quests.register',{id='multi',title='Rebuild',description='Recover the village',objectives={
            {id='supplies',title='Supplies',goal=2},{id='villagers',title='Villagers',goal=3}}}))
        assert(chronicle.call('quests.advance',{id='multi',objective='supplies',amount=1}).progress==1)
        assert(chronicle.call('quests.advance',{id='multi',objective='villagers',amount=2}).progress==3)
        assert(chronicle.quest_get('multi')==3)
        assert(not chronicle.quest_add('multi',1))
    end})",Json{{{"from","1"},{"rename_quests",{{"multi","renamed"}}}}});
    Start();runtime.Tick({1,2,0,true});ASSERT_TRUE(runtime.Diagnostics().empty());
    ASSERT_EQ(runtime.Quests().size(),1u);EXPECT_NE(runtime.Quests()[0].description.find("Supplies: 1 / 2"),std::string::npos);
    auto old=State();const auto before=runtime.SaveState();const auto epoch=runtime.Epoch();std::string error;
    auto bad=old;bad["mods"]["test.mod"]["quests"]["multi"]=4;EXPECT_FALSE(runtime.LoadState(bad.dump(),error));EXPECT_EQ(runtime.SaveState(),before);EXPECT_EQ(runtime.Epoch(),epoch);
    old["required"]["test.mod"]="1";ASSERT_TRUE(runtime.LoadState(old.dump(),error))<<error;
    const auto state=State()["mods"]["test.mod"];EXPECT_FALSE(state["quest_objectives"].contains("multi"));
    EXPECT_EQ(state["quest_objectives"]["renamed"],(Json{{"supplies",1},{"villagers",2}}));
    EXPECT_EQ(state["quests"]["renamed"],3);EXPECT_EQ(runtime.Quests()[0].goal,5);
    const auto saved=runtime.SaveState();runtime.ResetState();ASSERT_TRUE(runtime.LoadState(saved,error))<<error;EXPECT_EQ(runtime.SaveState(),saved);
}
TEST_F(ModDynamicQuestRuntime, MultiObjectiveChangesRollBackWithRejectedGameCommit) {
    Add("test.mod",R"(return {on_tick=function()
        assert(chronicle.call('quests.register',{id='multi',title='Rebuild',description='Recover the village',objectives={{id='supplies',title='Supplies',goal=2}}}))
        assert(chronicle.call('quests.advance',{id='multi',objective='supplies',amount=2}).complete)
        assert(chronicle.add_money(1))
    end})");Start();runtime.Tick({1,2,0,true},[](int){return false;});
    EXPECT_FALSE(runtime.Diagnostics().empty());const auto state=State()["mods"]["test.mod"];
    EXPECT_FALSE(state.contains("quests"));EXPECT_FALSE(state.contains("quest_objectives"));EXPECT_FALSE(state.contains("quest_definitions"));
}

}
