#include <gtest/gtest.h>
#include "mods/skills.hpp"
#include "mods/arguments.hpp"
#include "mods/runtime.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>

namespace {
using Json=nlohmann::json;
Json Tree() {
    return {{"id","combat"},{"title","Combat"},{"description","Training"},
        {"nodes",{{{"id","focus"},{"title","Focus"},{"description","Build focus"},{"max_rank",2}},
                  {{"id","power"},{"title","Power"},{"description","Build power"},{"max_rank",3},{"prerequisites",{{{"id","focus"},{"rank",2}}}}}}}};
}
TEST(ModSkills, ImmutableTreesPrerequisitesRankBoundsAndExplicitReset) {
    Json state=Json::object();const auto tree=Tree();
    auto registered=mods::CallOwnedSkill(state,"skills.register",tree);EXPECT_EQ(registered["nodes"][0]["rank"],0);
    EXPECT_EQ(mods::CallOwnedSkill(state,"skills.register",tree),registered);
    const auto before=state;EXPECT_THROW(mods::CallOwnedSkill(state,"skills.unlock",{{"tree","combat"},{"id","power"}}),std::runtime_error);EXPECT_EQ(state,before);
    auto changed=tree;changed["title"]="Changed";EXPECT_THROW(mods::CallOwnedSkill(state,"skills.register",changed),std::runtime_error);EXPECT_EQ(state,before);
    mods::CallOwnedSkill(state,"skills.unlock",{{"tree","combat"},{"id","focus"}});
    mods::CallOwnedSkill(state,"skills.unlock",{{"tree","combat"},{"id","focus"}});
    EXPECT_THROW(mods::CallOwnedSkill(state,"skills.unlock",{{"tree","combat"},{"id","focus"}}),std::runtime_error);
    EXPECT_EQ(mods::CallOwnedSkill(state,"skills.unlock",{{"tree","combat"},{"id","power"}})["rank"],1);
    mods::CallOwnedSkill(state,"skills.set_rank",{{"tree","combat"},{"id","focus"},{"rank",0}});
    const auto power=mods::CallOwnedSkill(state,"skills.get",{{"tree","combat"},{"id","power"}});
    EXPECT_EQ(power["rank"],1);EXPECT_EQ(power["available"],false);EXPECT_EQ(power["missing"][0]["id"],"focus");
    mods::CallOwnedSkill(state,"skills.set_rank",{{"tree","combat"},{"id","power"},{"rank",0}});
    EXPECT_NO_THROW(mods::ValidateSkillsState(state));
}
TEST(ModSkills, RejectsCyclesUnknownReferencesAndCorruptSavedRanks) {
    auto tree=Tree();tree["nodes"][0]["prerequisites"]={{{"id","power"},{"rank",1}}};EXPECT_FALSE(mods::ValidateSkillOperation("skills.register",tree).empty());
    tree=Tree();tree["nodes"][1]["prerequisites"][0]["id"]="missing";EXPECT_FALSE(mods::ValidateSkillOperation("skills.register",tree).empty());
    tree=Tree();tree["nodes"][1]["prerequisites"][0]["rank"]=3;EXPECT_FALSE(mods::ValidateSkillOperation("skills.register",tree).empty());
    tree=Tree();tree["nodes"][1]["id"]="focus";EXPECT_FALSE(mods::ValidateSkillOperation("skills.register",tree).empty());
    tree=Tree();tree["nodes"][0]["effects"]={{"damage",1}};EXPECT_FALSE(mods::ValidateSkillOperation("skills.register",tree).empty());
    Json state=Json::object();mods::CallOwnedSkill(state,"skills.register",Tree());
    for(const Json &rank:std::vector<Json>{-1,3,true,"1"}){auto bad=state;bad["skill_ranks"]["combat"]["focus"]=rank;EXPECT_THROW(mods::ValidateSkillsState(bad),std::runtime_error);}
    auto bad=state;bad["skill_ranks"]["foreign"]["focus"]=1;EXPECT_THROW(mods::ValidateSkillsState(bad),std::runtime_error);
}
TEST(ModSkills, TreeCapacityAndNodeCapacityAreBounded) {
    Json state=Json::object();for(int i=0;i<32;++i){auto tree=Tree();tree["id"]="tree"+std::to_string(i);mods::CallOwnedSkill(state,"skills.register",tree);}
    const auto before=state;EXPECT_THROW(mods::CallOwnedSkill(state,"skills.register",Tree()),std::runtime_error);EXPECT_EQ(state,before);
    auto tree=Tree();tree["nodes"]=Json::array();for(int i=0;i<129;++i)tree["nodes"].push_back({{"id","node"+std::to_string(i)},{"title","Title"},{"description","Description"},{"max_rank",1}});
    EXPECT_FALSE(mods::ValidateSkillOperation("skills.register",tree).empty());
}
TEST(ModSkills, LargerTransportBudgetIsLimitedToTreeRegistration) {
    auto tree=Tree();tree["nodes"]=Json::array();for(int i=0;i<128;++i)tree["nodes"].push_back({{"id","node"+std::to_string(i)},{"title","Node"},{"description",std::string(256,'x')},{"max_rank",99}});
    EXPECT_FALSE(mods::ValidateArguments(tree).empty());EXPECT_TRUE(mods::ValidateArguments(tree,mods::OperationArgumentLimits("skills.register")).empty());
    EXPECT_EQ(mods::OperationArgumentLimits("economy.add").nodes,128u);EXPECT_EQ(mods::OperationArgumentLimits("skills.set_rank").bytes,16384u);
}
class ModSkillsRuntime:public testing::Test {
protected:
    std::filesystem::path root=std::filesystem::temp_directory_path()/("chronicle-skills-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    mods::Runtime runtime;mods::Manager manager;std::map<std::string,bool> enabled;
    void TearDown()override{runtime.Start(mods::Manager{});std::error_code error;std::filesystem::remove_all(root,error);}
    void Add(const std::string &id,const std::string &script) {
        const auto path=root/id;std::filesystem::create_directories(path);
        std::ofstream(path/"mod.json")<<Json{{"schema_version",1},{"api_version",1},{"id",id},{"name",id},{"version","1"},{"game","chronicle"},{"files",Json::object()},
            {"script","main.lua"},{"permissions",{"game.read","game.write","economy.write"}},{"requires_capabilities",{"skills.trees.v1"}}}.dump();
        std::ofstream(path/"main.lua")<<script;enabled[id]=true;
    }
    void Start(){manager.Scan({root},{},enabled);runtime.Start(manager);}
    std::string Register()const{return "assert(chronicle.call('skills.register',{id='combat',title='Combat',description='Training',nodes={{id='focus',title='Focus',description='Build focus',max_rank=2}}})); ";}
};
TEST_F(ModSkillsRuntime, SaveRestoreOwnerIsolationAndMenuSnapshots) {
    for(const auto id:{"test.one","test.two"})Add(id,"return {on_tick=function() "+Register()+
        "assert(chronicle.call('skills.set_rank',{tree='combat',id='focus',rank="+(std::string(id)=="test.one"?"1":"2")+"})); end}");
    Start();runtime.Tick({1,2,0,true});ASSERT_TRUE(runtime.Diagnostics().empty());ASSERT_EQ(runtime.Skills().size(),2u);
    EXPECT_EQ(runtime.Skills()[0].rank,1);EXPECT_EQ(runtime.Skills()[1].rank,2);EXPECT_TRUE(runtime.Skills()[0].available);EXPECT_FALSE(runtime.Skills()[1].available);
    const auto before=runtime.SaveState();auto bad=Json::parse(before);bad["mods"]["test.one"]["skill_ranks"]["combat"]["focus"]=99;
    std::string error;EXPECT_FALSE(runtime.LoadState(bad.dump(),error));EXPECT_EQ(runtime.SaveState(),before);
    runtime.ResetState();EXPECT_TRUE(runtime.Skills().empty());ASSERT_TRUE(runtime.LoadState(before,error))<<error;EXPECT_EQ(runtime.SaveState(),before);
    runtime.Start(mods::Manager{});EXPECT_TRUE(runtime.Skills().empty());EXPECT_FALSE(runtime.HasGameplay());
}
TEST_F(ModSkillsRuntime, CallbackFailureAndRejectedCommitRollBackSkills) {
    for(bool callback:{false,true}) {
        Add("test.mod","return {on_tick=function() "+Register()+"assert(chronicle.call('skills.unlock',{tree='combat',id='focus'})); assert(chronicle.add_money(1)); "+(callback?"error('rollback')":"")+" end}");
        Start();runtime.ResetState();runtime.Tick({1,2,0,true},[](int){return false;});
        EXPECT_FALSE(runtime.Diagnostics().empty());const auto state=Json::parse(runtime.SaveState())["mods"]["test.mod"];
        EXPECT_FALSE(state.contains("skill_trees"));EXPECT_FALSE(state.contains("skill_ranks"));
    }
}
TEST_F(ModSkillsRuntime, FullTreeCanBeRegisteredThroughLuaTransport) {
    Add("test.mod",R"(return {on_tick=function()
        local nodes={}
        for i=1,128 do nodes[i]={id='node'..i,title='Node',description=string.rep('x',256),max_rank=99} end
        assert(chronicle.call('skills.register',{id='full',title='Full tree',description='All nodes',nodes=nodes}))
        assert(#chronicle.call('skills.list',{}).trees[1].nodes==128)
    end})");Start();runtime.Tick({1,2,0,true});EXPECT_TRUE(runtime.Diagnostics().empty());EXPECT_EQ(runtime.Skills().size(),128u);
}

}
