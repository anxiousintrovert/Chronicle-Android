#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "mods/quests.hpp"
#include "mods/api.hpp"
#include "savedata.hpp"
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

TEST(ModDynamicQuests, ExplicitLifecycleRequiresStartAndCannotRestartCompletedQuest) {
    auto definition=MultiDefinition();definition["explicit_start"]=true;Json state=Json::object();
    auto status=mods::CallOwnedQuest({},state,"quests.register",definition);EXPECT_EQ(status["status"],"registered");
    const auto before=state;
    EXPECT_THROW(mods::CallOwnedQuest({},state,"quests.advance",{{"id","multi"},{"objective","supplies"},{"amount",1}}),std::runtime_error);EXPECT_EQ(state,before);
    status=mods::CallOwnedQuest({},state,"quests.start",{{"id","multi"}});EXPECT_EQ(status["status"],"active");
    const auto started=state;EXPECT_EQ(mods::CallOwnedQuest({},state,"quests.start",{{"id","multi"}}),status);EXPECT_EQ(state,started);
    mods::CallOwnedQuest({},state,"quests.advance",{{"id","multi"},{"objective","supplies"},{"amount",2}});
    status=mods::CallOwnedQuest({},state,"quests.advance",{{"id","multi"},{"objective","villagers"},{"amount",3}});EXPECT_EQ(status["status"],"completed");
    const auto complete=state;EXPECT_THROW(mods::CallOwnedQuest({},state,"quests.start",{{"id","multi"}}),std::runtime_error);EXPECT_EQ(state,complete);
    auto bad=state;bad.erase("quest_started");EXPECT_THROW(mods::QuestDefinitions({},bad),std::runtime_error);
    for(const Json &value:std::vector<Json>{false,1,"true"}){bad=state;bad["quest_started"]["multi"]=value;EXPECT_THROW(mods::QuestDefinitions({},bad),std::runtime_error);}
    bad=state;bad["quest_started"]["foreign"]=true;EXPECT_THROW(mods::QuestDefinitions({},bad),std::runtime_error);
    definition["explicit_start"]=1;EXPECT_FALSE(mods::ValidateQuestOperation("quests.register",definition).empty());
    EXPECT_FALSE(mods::ValidateQuestOperation("quests.start",{{"id","multi"},{"owner","other"}}).empty());
    Json legacy=Json::object();EXPECT_EQ(mods::CallOwnedQuest({},legacy,"quests.register",Definition())["status"],"active");
    const auto legacyBefore=legacy;mods::CallOwnedQuest({},legacy,"quests.start",{{"id","dynamic"}});EXPECT_EQ(legacy,legacyBefore);
}
TEST(ModDynamicQuests, RewardValidationAndClaimRecordsPreventReplay) {
    auto definition=Definition();definition["rewards"]={{{"operation","economy.add"},{"arguments",{{"amount",7}}}}};
    Json state=Json::object();mods::CallOwnedQuest({},state,"quests.register",definition);const auto before=state;
    EXPECT_THROW(mods::CallOwnedQuest({},state,"quests.advance",{{"id","dynamic"},{"amount",3}}),std::runtime_error);EXPECT_EQ(state,before);
    int grants=0;auto grant=[&](const Json &rewards){EXPECT_EQ(rewards,definition["rewards"]);++grants;};
    auto result=mods::CallOwnedQuest({},state,"quests.advance",{{"id","dynamic"},{"amount",3}},grant);EXPECT_EQ(result["rewards_granted"],true);
    mods::CallOwnedQuest({},state,"quests.advance",{{"id","dynamic"},{"amount",3}},grant);EXPECT_EQ(grants,1);
    auto bad=state;bad.erase("quest_rewarded");EXPECT_THROW(mods::QuestDefinitions({},bad),std::runtime_error);
    bad=before;bad["quest_rewarded"]["dynamic"]=true;EXPECT_THROW(mods::QuestDefinitions({},bad),std::runtime_error);
    bad=state;bad["quest_rewarded"]["foreign"]=true;EXPECT_THROW(mods::QuestDefinitions({},bad),std::runtime_error);
    for(const Json &rewards:std::vector<Json>{Json::array(),{{{"operation","economy.spend"},{"arguments",{{"amount",1}}}}},{{{"operation","economy.add"},{"arguments",{{"amount",0}}}}}}) {
        auto invalid=definition;invalid["rewards"]=rewards;EXPECT_FALSE(mods::ValidateQuestOperation("quests.register",invalid).empty());
    }
}
TEST(ModDynamicQuests, EventBindingsValidateNamesAndExactFieldFilters) {
    auto definition=MultiDefinition();auto &objective=definition["objectives"][0];
    objective["event"]="enemy_killed";objective["match"]={{"source","native"},{"attacker",0},{"floor",2}};
    EXPECT_TRUE(mods::ValidateQuestOperation("quests.register",definition).empty());Json state=Json::object();mods::CallOwnedQuest({},state,"quests.register",definition);
    EXPECT_TRUE(mods::WantsQuestEvent(state,"enemy_killed"));EXPECT_FALSE(mods::WantsQuestEvent(state,"floor_enter"));
    EXPECT_EQ(mods::QuestEventAdvances({},state,"enemy_killed",{{"source","native"},{"attacker",0},{"floor",2}}).size(),1u);
    EXPECT_TRUE(mods::QuestEventAdvances({},state,"enemy_killed",{{"source","scripted"},{"attacker",0},{"floor",2}}).empty());
    for(const Json &change:std::vector<Json>{{{"event","unknown"}},{{"match",{{"unknown",1}}}},{{"match",{{"source","spoof"}}}},{{"match",{{"floor",true}}}},{{"event","map_enter"}}}) {
        auto bad=definition;bad["objectives"][0].update(change);EXPECT_FALSE(mods::ValidateQuestOperation("quests.register",bad).empty());
    }
    objective.erase("event");EXPECT_FALSE(mods::ValidateQuestOperation("quests.register",definition).empty());
}
TEST(ModDynamicQuests, EventWorkBudgetRejectsBeforeChangingAnyCounter) {
    Json state=Json::object();
    for(int q=0;q<5;++q) {
        Json definition={{"id","quest"+std::to_string(q)},{"title","Title"},{"description","Description"},{"objectives",Json::array()}};
        for(int o=0;o<13;++o)definition["objectives"].push_back({{"id","objective"+std::to_string(o)},{"title","Kill"},{"goal",1},{"event","enemy_killed"}});
        mods::CallOwnedQuest({},state,"quests.register",definition);
    }
    const auto before=state;EXPECT_THROW(mods::QuestEventAdvances({},state,"enemy_killed",Json::object()),std::runtime_error);EXPECT_EQ(state,before);
    state["quest_definitions"].erase("quest4");EXPECT_EQ(mods::QuestEventAdvances({},state,"enemy_killed",Json::object()).size(),52u);
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
        assert(chronicle.call('quests.register',{id='multi',title='Rebuild',description='Recover the village',explicit_start=true,objectives={
            {id='supplies',title='Supplies',goal=2},{id='villagers',title='Villagers',goal=3}}}))
        assert(chronicle.call('quests.start',{id='multi'}).status=='active')
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
    EXPECT_EQ(state["quest_started"]["renamed"],true);EXPECT_FALSE(state["quest_started"].contains("multi"));
    EXPECT_EQ(state["quests"]["renamed"],3);EXPECT_EQ(runtime.Quests()[0].goal,5);
    const auto saved=runtime.SaveState();runtime.ResetState();ASSERT_TRUE(runtime.LoadState(saved,error))<<error;EXPECT_EQ(runtime.SaveState(),saved);
}
TEST_F(ModDynamicQuestRuntime, MultiObjectiveChangesRollBackWithRejectedGameCommit) {
    Add("test.mod",R"(return {on_tick=function()
        assert(chronicle.call('quests.register',{id='multi',title='Rebuild',description='Recover the village',explicit_start=true,objectives={{id='supplies',title='Supplies',goal=2}}}))
        assert(chronicle.call('quests.start',{id='multi'}).status=='active')
        assert(chronicle.call('quests.advance',{id='multi',objective='supplies',amount=2}).complete)
        assert(chronicle.add_money(1))
    end})");Start();runtime.Tick({1,2,0,true},[](int){return false;});
    EXPECT_FALSE(runtime.Diagnostics().empty());const auto state=State()["mods"]["test.mod"];
    EXPECT_FALSE(state.contains("quests"));EXPECT_FALSE(state.contains("quest_objectives"));EXPECT_FALSE(state.contains("quest_definitions"));EXPECT_FALSE(state.contains("quest_started"));
}

TEST_F(ModDynamicQuestRuntime, RewardsCommitOnceAndMigrateWithoutRegranting) {
    Add("test.mod",R"(return {on_tick=function()
        assert(chronicle.call('quests.register',{id='reward',title='Reward',description='Finish this quest',goal=1,
            rewards={{operation='economy.add',arguments={amount=7}},
                     {operation='inventory.give',arguments={item=132,quantity=1,seed=42}},
                     {operation='weapons.give',arguments={item=257,seed=42}}}}))
        assert(chronicle.call('quests.advance',{id='reward',amount=1}).rewards_granted)
    end})",Json{{{"from","1"},{"rename_quests",{{"reward","renamed"}}}}});
    Start();auto save=std::make_unique<CSaveData>();save->GetDngStatus()->money=0;int commits=0;
    mods::CommitGame commit=[&](const auto &commands,int money,std::string &error){++commits;EXPECT_EQ(commands.size(),3u);for(const auto &command:commands)EXPECT_EQ(command.owner,"test.mod");return mods::ApplySave(*save,commands,money,error);};
    runtime.Tick({1,2,0,true},{},{},{},commit);ASSERT_TRUE(runtime.Diagnostics().empty());EXPECT_EQ(commits,1);EXPECT_EQ(save->GetDngStatus()->money,7);
    runtime.Tick({1,2,7,true},{},{},{},commit);EXPECT_EQ(commits,1);
    auto old=State();old["required"]["test.mod"]="1";std::string error;ASSERT_TRUE(runtime.LoadState(old.dump(),error))<<error;
    EXPECT_EQ(State()["mods"]["test.mod"]["quest_rewarded"]["renamed"],true);EXPECT_FALSE(State()["mods"]["test.mod"]["quest_rewarded"].contains("reward"));
    const auto saved=runtime.SaveState();runtime.ResetState();ASSERT_TRUE(runtime.LoadState(saved,error))<<error;EXPECT_EQ(runtime.SaveState(),saved);
}
TEST_F(ModDynamicQuestRuntime, RewardFailureRollsBackProgressAndCallbackCommands) {
    const std::string script=R"(return {on_tick=function()
        assert(chronicle.call('quests.register',{id='reward',title='Reward',description='Finish this quest',goal=1,
            rewards={{operation='economy.add',arguments={amount=7}}}}))
        assert(chronicle.quest_add('reward',1)==1)
    end})";
    Add("test.mod",script);Start();int commits=0;
    runtime.Tick({1,2,0,true},{},{},{},[&](const auto &,int,std::string &error){++commits;error="reward rejected";return false;});
    EXPECT_EQ(commits,1);EXPECT_FALSE(runtime.Diagnostics().empty());const auto state=State()["mods"]["test.mod"];
    EXPECT_FALSE(state.contains("quest_rewarded"));EXPECT_FALSE(state.contains("quest_definitions"));EXPECT_FALSE(state.contains("quests"));
}

TEST_F(ModDynamicQuestRuntime, DeniedRewardDoesNotLeakEarlierGrantsOrProgressWhenCaught) {
    Add("test.mod",R"(return {on_tick=function()
        assert(chronicle.call('quests.register',{id='reward',title='Reward',description='Finish',goal=1,
          rewards={{operation='inventory.give',arguments={item=132,quantity=1,seed=42}},
                   {operation='economy.add',arguments={amount=7}}}}))
        local result,err=chronicle.call('quests.advance',{id='reward',amount=1})
        assert(not result and string.find(err,'permission denied'))
        assert(chronicle.call('quests.get',{id='reward'}).progress==0)
        assert(chronicle.set('caught',true))
    end})");
    auto manifestPath=root/"test.mod/mod.json";Json manifest;{std::ifstream in(manifestPath);in>>manifest;}manifest["permissions"]={"game.read","game.write"};std::ofstream(manifestPath)<<manifest.dump();
    Start();int commits=0;runtime.Tick({1,2,0,true},{},{},{},[&](const auto &,int,std::string &){++commits;return true;});
    EXPECT_TRUE(runtime.Diagnostics().empty());EXPECT_EQ(commits,0);const auto state=State()["mods"]["test.mod"];
    EXPECT_EQ(state["values"]["caught"],true);EXPECT_FALSE(state.contains("quest_rewarded"));EXPECT_FALSE(state.contains("quests"));
}
TEST_F(ModDynamicQuestRuntime, CallbackErrorAfterRewardQueuesNoNativeWrite) {
    Add("test.mod",R"(return {on_tick=function()
        assert(chronicle.call('quests.register',{id='reward',title='Reward',description='Finish',goal=1,rewards={{operation='economy.add',arguments={amount=7}}}}))
        assert(chronicle.call('quests.advance',{id='reward',amount=1}).rewards_granted)
        error('reject after reward')
    end})");Start();int commits=0;runtime.Tick({1,2,0,true},{},{},{},[&](const auto &,int,std::string &){++commits;return true;});
    EXPECT_FALSE(runtime.Diagnostics().empty());EXPECT_EQ(commits,0);EXPECT_FALSE(State()["mods"]["test.mod"].contains("quest_rewarded"));
}

TEST_F(ModDynamicQuestRuntime, RewardsShareCallbackBudgetWithoutLeakingPartialBatch) {
    Add("test.mod",R"(return {on_tick=function()
        assert(chronicle.call('quests.register',{id='reward',title='Reward',description='Finish',goal=1,
          rewards={{operation='economy.add',arguments={amount=7}},{operation='economy.add',arguments={amount=8}}}}))
        for i=1,63 do assert(chronicle.call('economy.add',{amount=1})) end
        local result,err=chronicle.call('quests.advance',{id='reward',amount=1})
        assert(not result and string.find(err,'budget'))
        assert(chronicle.call('quests.get',{id='reward'}).progress==0)
    end})");Start();int commits=0;runtime.Tick({1,2,0,true},{},{},{},[&](const auto &commands,int,std::string &){++commits;EXPECT_EQ(commands.size(),63u);for(const auto &command:commands)EXPECT_EQ(command.arguments["amount"],1);return true;});
    EXPECT_TRUE(runtime.Diagnostics().empty());EXPECT_EQ(commits,1);EXPECT_FALSE(State()["mods"]["test.mod"].contains("quest_rewarded"));
}

TEST_F(ModDynamicQuestRuntime, NativeDeathObjectivesStartFilterCompleteAndStopCapture) {
    Add("test.mod",R"(return {on_game_enter=function()
        assert(chronicle.call('quests.register',{id='hunt',title='Hunt',description='Two native kills',explicit_start=true,
          objectives={{id='kills',title='Kills',goal=2,event='enemy_killed',match={source='native',attacker=0}}},
          rewards={{operation='economy.add',arguments={amount=7}}}}))
    end,on_tick=function() if chronicle.get('start') then assert(chronicle.call('quests.start',{id='hunt'})) end end})");
    Start();const mods::World world{2,0,0,true,0,1};runtime.Tick(world);EXPECT_FALSE(runtime.WantsEnemyDeaths());
    auto saved=State();saved["mods"]["test.mod"]["quest_started"]["hunt"]=true;std::string error;ASSERT_TRUE(runtime.LoadState(saved.dump(),error))<<error;EXPECT_TRUE(runtime.WantsEnemyDeaths());
    const std::array<mods::EnemyDeath,1> deaths{{{16,0,1,0,10,20,5,false}}};int commits=0;
    mods::CommitGame commit=[&](const auto &commands,int,std::string &){++commits;EXPECT_EQ(commands.size(),1u);EXPECT_EQ(commands[0].arguments["amount"],7);return true;};
    runtime.Tick(world,{},{},{},commit,deaths);EXPECT_FALSE(State()["mods"]["test.mod"].contains("quests")); // load suppresses replay
    auto wrong=deaths;wrong[0].scripted=true;runtime.Tick(world,{},{},{},commit,wrong);EXPECT_FALSE(State()["mods"]["test.mod"].contains("quests"));
    wrong=deaths;wrong[0].floor=2;runtime.Tick(world,{},{},{},commit,wrong);EXPECT_FALSE(State()["mods"]["test.mod"].contains("quests"));
    runtime.Tick(world,{},{},{},commit,deaths);EXPECT_EQ(State()["mods"]["test.mod"]["quests"]["hunt"],1);EXPECT_TRUE(runtime.WantsEnemyDeaths());
    runtime.Tick(world,{},{},{},commit,deaths);EXPECT_EQ(State()["mods"]["test.mod"]["quests"]["hunt"],2);EXPECT_EQ(commits,1);EXPECT_FALSE(runtime.WantsEnemyDeaths());
    runtime.Tick(world,{},{},{},commit,deaths);EXPECT_EQ(commits,1);EXPECT_TRUE(runtime.Diagnostics().empty());
}
TEST_F(ModDynamicQuestRuntime, MapAndFloorBindingsObserveTransitionsButNotSaveReload) {
    Add("test.mod",R"(return {on_game_enter=function()
        assert(chronicle.call('quests.register',{id='travel',title='Travel',description='Explore',objectives={
          {id='map',title='Map',goal=3,event='map_enter',match={map=4}},
          {id='floor',title='Floor',goal=3,event='floor_enter',match={dungeon=0,floor=2}}}}))
    end,on_floor_enter=function()
        local q=chronicle.call('quests.get',{id='travel'})
        assert(chronicle.set('seen',q.progress))
    end})");Start();runtime.Tick({2,4,0,true,0,2});auto state=State()["mods"]["test.mod"];
    EXPECT_EQ(state["quests"]["travel"],2);EXPECT_EQ(state["values"]["seen"],2);EXPECT_FALSE(runtime.WantsEnemyDeaths());
    runtime.Tick({2,4,0,true,0,2});EXPECT_EQ(State()["mods"]["test.mod"]["quests"]["travel"],2);
    std::string error;ASSERT_TRUE(runtime.LoadState(runtime.SaveState(),error))<<error;runtime.Tick({2,4,0,true,0,2});EXPECT_EQ(State()["mods"]["test.mod"]["quests"]["travel"],2);
    runtime.Tick({2,3,0,true,0,1});runtime.Tick({2,4,0,true,0,2});EXPECT_EQ(State()["mods"]["test.mod"]["quests"]["travel"],4);EXPECT_TRUE(runtime.Diagnostics().empty());
}
TEST_F(ModDynamicQuestRuntime, EventRewardFailureRollsBackAllMatchingQuestsAndDisablesOwner) {
    Add("test.mod",R"(return {on_game_enter=function()
        for _,id in ipairs({'one','two'}) do assert(chronicle.call('quests.register',{id=id,title=id,description='Hunt',
            objectives={{id='kill',title='Kill',goal=1,event='enemy_killed'}},rewards={{operation='economy.add',arguments={amount=7}}}})) end
    end})");Start();const mods::World world{2,0,0,true,0,1};runtime.Tick(world);const auto before=runtime.SaveState();
    const std::array<mods::EnemyDeath,1> deaths{{{16,0,1}}};int commits=0;
    runtime.Tick(world,{},{},{},[&](const auto &commands,int,std::string &error){++commits;EXPECT_EQ(commands.size(),2u);error="full";return false;},deaths);
    EXPECT_EQ(commits,1);EXPECT_EQ(runtime.SaveState(),before);EXPECT_FALSE(runtime.Diagnostics().empty());EXPECT_FALSE(runtime.WantsEnemyDeaths());
}

}

TEST_F(ModDynamicQuestRuntime, ShippedContractStartsFromMenuAndRestoresWithoutRewardReplay) {
    const auto source=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path()/"examples/mods/dungeon-contract";
    std::filesystem::create_directories(root/"contract");
    for(const auto *file:{"mod.json","main.lua"})std::filesystem::copy_file(source/file,root/"contract"/file);
    Start();EXPECT_FALSE(runtime.HasGameplay()); // New example packages remain disabled.
    enabled["example.dungeon-contract"]=true;Start();ASSERT_TRUE(runtime.Diagnostics().empty());
    const mods::World floor1{2,0,0,true,0,1},floor2{2,0,0,true,0,2};
    auto save=std::make_unique<CSaveData>();save->GetDngStatus()->money=0;int grants=0;
    mods::CommitGame commit=[&](const auto &commands,int money,std::string &error){
        ++grants;EXPECT_EQ(commands.size(),1u);return mods::ApplySave(*save,commands,money,error);
    };
    runtime.Tick(floor1,{},{},{},commit);
    ASSERT_EQ(runtime.Quests().size(),1u);EXPECT_EQ(runtime.Quests()[0].progress,0);
    EXPECT_FALSE(runtime.WantsEnemyDeaths());
    std::string error;ASSERT_TRUE(runtime.QueueAction("example.dungeon-contract","start",error))<<error;
    EXPECT_FALSE(runtime.WantsEnemyDeaths());runtime.Tick(floor1,{},{},{},commit);
    EXPECT_TRUE(runtime.WantsEnemyDeaths());EXPECT_EQ(grants,0);
    runtime.Tick(floor2,{},{},{},commit);EXPECT_EQ(runtime.Quests()[0].progress,1);
    const auto partial=runtime.SaveState();runtime.ResetState();ASSERT_TRUE(runtime.LoadState(partial,error))<<error;
    runtime.Tick(floor2,{},{},{},commit);EXPECT_EQ(runtime.Quests()[0].progress,1);
    std::array<mods::EnemyDeath,1> death{{{16,0,2,0,10,20,5,true}}};
    runtime.Tick(floor2,{},{},{},commit,death);EXPECT_EQ(runtime.Quests()[0].progress,1);
    death[0].scripted=false;runtime.Tick(floor2,{},{},{},commit,death);EXPECT_EQ(runtime.Quests()[0].progress,2);
    runtime.Tick(floor2,{},{},{},commit,death);EXPECT_EQ(runtime.Quests()[0].progress,3);
    EXPECT_EQ(grants,1);EXPECT_EQ(save->GetDngStatus()->money,50);EXPECT_FALSE(runtime.WantsEnemyDeaths());
    const auto completed=runtime.SaveState();runtime.ResetState();ASSERT_TRUE(runtime.LoadState(completed,error))<<error;
    runtime.Tick(floor2,{},{},{},commit);ASSERT_TRUE(runtime.QueueAction("example.dungeon-contract","start",error))<<error;
    runtime.Tick(floor2,{},{},{},commit,death);EXPECT_EQ(grants,1);EXPECT_EQ(save->GetDngStatus()->money,50);
    EXPECT_EQ(runtime.SaveState(),completed);EXPECT_TRUE(runtime.Diagnostics().empty());
}
