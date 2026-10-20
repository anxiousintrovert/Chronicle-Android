#include <gtest/gtest.h>
#include "mods/skills.hpp"
#include "mods/arguments.hpp"
#include "mods/runtime.hpp"
#include <chrono>
#include <bit>
#include <limits>
#include "mods/combat_damage.hpp"
#include "mods/entities.hpp"
#include "mods/entity_tracking.hpp"
#include "mods/entity_reactions.hpp"
#include "monstorunit.hpp"
#include "userstatus.hpp"
#include "dun/gameloop.hpp"
#include "main.hpp"
extern s32 mode;
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
TEST(ModSkills, EffectValidationAndOnlyTrainedPositiveFactors) {
    auto tree=Tree();tree["nodes"][0]["effects"]={{"enemy_damage_per_rank",0.25}};
    EXPECT_TRUE(mods::ValidateSkillOperation("skills.register",tree).empty());
    for(const Json &value:std::vector<Json>{-0.1,1.1,true,"0.1",std::numeric_limits<double>::infinity()}) {
        auto bad=tree;bad["nodes"][0]["effects"]["enemy_damage_per_rank"]=value;EXPECT_FALSE(mods::ValidateSkillOperation("skills.register",bad).empty());
    }
    Json state=Json::object();mods::CallOwnedSkill(state,"skills.register",tree);EXPECT_TRUE(mods::SkillDamageFactors(state).empty());
    mods::CallOwnedSkill(state,"skills.set_rank",{{"tree","combat"},{"id","focus"},{"rank",2}});
    EXPECT_DOUBLE_EQ(mods::SkillDamageFactors(state).at({"combat","focus"}),1.5);
    mods::CallOwnedSkill(state,"skills.set_rank",{{"tree","combat"},{"id","focus"},{"rank",0}});EXPECT_TRUE(mods::SkillDamageFactors(state).empty());
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
TEST_F(ModSkillsRuntime, TrainingIsDeferredOwnedAndDoesNotImplicitlyChangeRanks) {
    for(const auto id:{"test.one","test.two"})Add(id,
        "chronicle.subscribe('skill_train_requested',function(w) assert(w.event.tree=='combat' and w.event.id=='focus'); chronicle.set('requests',(chronicle.get('requests') or 0)+1) end); return {on_tick=function() "+Register()+"end}");
    Start();std::string error;EXPECT_FALSE(runtime.QueueSkillTraining("test.one","combat","focus",error));
    const mods::World world{2,0,0,true,0,1};runtime.Tick(world);
    const auto before=runtime.SaveState();ASSERT_TRUE(runtime.QueueSkillTraining("test.one","combat","focus",error))<<error;
    EXPECT_FALSE(runtime.QueueSkillTraining("test.one","combat","focus",error));EXPECT_FALSE(runtime.QueueSkillTraining("test.one","combat","missing",error));
    EXPECT_EQ(runtime.SaveState(),before);runtime.Tick(world);ASSERT_TRUE(runtime.Diagnostics().empty());
    const auto state=Json::parse(runtime.SaveState())["mods"];
    EXPECT_EQ(state["test.one"]["values"]["requests"],1);EXPECT_FALSE(state["test.two"]["values"].contains("requests"));
    EXPECT_EQ(runtime.Skills()[0].rank,0);runtime.Tick(world);EXPECT_EQ(Json::parse(runtime.SaveState())["mods"]["test.one"]["values"]["requests"],1);
}
TEST_F(ModSkillsRuntime, TrainingCancelsAcrossContextLoadResetAndUnavailableOwners) {
    Add("test.mod","chronicle.subscribe('skill_train_requested',function(w) chronicle.set('trained',true) end); return {on_tick=function() "+Register()+"end}");
    Start();const mods::World world{2,0,0,true,0,1};runtime.Tick(world);std::string error;
    ASSERT_TRUE(runtime.QueueSkillTraining("test.mod","combat","focus",error));runtime.Tick({2,0,0,true,0,2});
    EXPECT_FALSE(Json::parse(runtime.SaveState())["mods"]["test.mod"]["values"].contains("trained"));
    ASSERT_TRUE(runtime.QueueSkillTraining("test.mod","combat","focus",error));const auto saved=runtime.SaveState();ASSERT_TRUE(runtime.LoadState(saved,error));runtime.Tick(world);EXPECT_EQ(runtime.SaveState(),saved);
    ASSERT_TRUE(runtime.QueueSkillTraining("test.mod","combat","focus",error));runtime.ResetState();runtime.Tick(world);EXPECT_FALSE(Json::parse(runtime.SaveState())["mods"]["test.mod"]["values"].contains("trained"));
    runtime.Start(mods::Manager{});EXPECT_FALSE(runtime.QueueSkillTraining("test.mod","combat","focus",error));
}
TEST_F(ModSkillsRuntime, TrainingQueueIsBoundedAndSuccessfulHandlersChooseRankWrites) {
    Add("test.mod",R"(chronicle.subscribe('skill_train_requested',function(w) assert(chronicle.call('skills.unlock',w.event)) end)
        return {on_tick=function() local nodes={};for i=1,17 do nodes[i]={id='node'..i,title='Node',description='Training',max_rank=2} end
        assert(chronicle.call('skills.register',{id='combat',title='Combat',description='Training',nodes=nodes})) end})");
    Add("test.passive","return {on_tick=function() "+Register()+"end}");
    Start();const mods::World world{2,0,0,true,0,1};runtime.Tick(world);std::string error;
    EXPECT_FALSE(runtime.QueueSkillTraining("test.passive","combat","focus",error));EXPECT_NE(error.find("no training handler"),std::string::npos);
    for(int i=1;i<=16;++i)ASSERT_TRUE(runtime.QueueSkillTraining("test.mod","combat","node"+std::to_string(i),error))<<error;
    EXPECT_FALSE(runtime.QueueSkillTraining("test.mod","combat","node17",error));
    runtime.Tick(world);ASSERT_TRUE(runtime.Diagnostics().empty());
    const auto ranks=Json::parse(runtime.SaveState())["mods"]["test.mod"]["skill_ranks"]["combat"];EXPECT_EQ(ranks.size(),16u);
    for(int i=1;i<=16;++i)EXPECT_EQ(ranks["node"+std::to_string(i)],1);
    ASSERT_TRUE(runtime.QueueSkillTraining("test.mod","combat","node17",error));runtime.Tick(world);EXPECT_TRUE(runtime.Diagnostics().empty());
    EXPECT_EQ(Json::parse(runtime.SaveState())["mods"]["test.mod"]["skill_ranks"]["combat"]["node17"],1);
}
TEST_F(ModSkillsRuntime, TrainingHandlerRankAndCurrencyChangesRollbackTogether) {
    Add("test.mod","chronicle.subscribe('skill_train_requested',function(w) assert(chronicle.call('skills.unlock',w.event)); assert(chronicle.add_money(-5)) end); return {on_tick=function() "+Register()+"end}");
    Start();const mods::World world{2,0,0,true,0,1};runtime.Tick(world);std::string error;
    ASSERT_TRUE(runtime.QueueSkillTraining("test.mod","combat","focus",error));runtime.Tick(world,[](int amount){EXPECT_EQ(amount,-5);return false;});
    EXPECT_FALSE(runtime.Diagnostics().empty());EXPECT_FALSE(Json::parse(runtime.SaveState())["mods"]["test.mod"].contains("skill_ranks"));
    EXPECT_FALSE(runtime.QueueSkillTraining("test.mod","combat","focus",error));
}
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

TEST_F(ModSkillsRuntime, DamageEffectsStackPreserveNonHitsRestoreAndDisable) {
    for(const auto id:{"test.beta","test.alpha"})Add(id,R"(return {on_tick=function()
        assert(chronicle.call('skills.register',{id='combat',title='Combat',description='Training',nodes={
          {id='focus',title='Focus',description='Focus',max_rank=2,effects={enemy_damage_per_rank=0.25}}}}))
        assert(chronicle.call('skills.set_rank',{tree='combat',id='focus',rank=2}))
    end})");Start();EXPECT_FALSE(runtime.HasSkillDamageEffects());runtime.Tick({1,2,0,true});ASSERT_TRUE(runtime.Diagnostics().empty());
    EXPECT_TRUE(runtime.HasSkillDamageEffects());EXPECT_FLOAT_EQ(runtime.ApplySkillDamage(100),225);
    for(float value:{0.0f,-0.0f,-5.0f,std::numeric_limits<float>::infinity()})EXPECT_EQ(std::bit_cast<unsigned>(runtime.ApplySkillDamage(value)),std::bit_cast<unsigned>(value));
    EXPECT_EQ(runtime.ApplySkillDamage(900000),1000000);const auto saved=runtime.SaveState();runtime.ResetState();EXPECT_FALSE(runtime.HasSkillDamageEffects());
    std::string error;ASSERT_TRUE(runtime.LoadState(saved,error))<<error;EXPECT_FLOAT_EQ(runtime.ApplySkillDamage(100),225);
    runtime.Start(mods::Manager{});EXPECT_FALSE(runtime.HasSkillDamageEffects());EXPECT_FLOAT_EQ(runtime.ApplySkillDamage(100),100);
}
TEST_F(ModSkillsRuntime, StagedRankAffectsCommitButFailedOwnerLeavesNoCachedBonus) {
    Add("test.mod",R"(return {on_tick=function()
        assert(chronicle.call('skills.register',{id='combat',title='Combat',description='Training',nodes={
          {id='focus',title='Focus',description='Focus',max_rank=2,effects={enemy_damage_per_rank=1,incoming_damage_reduction_per_rank=0.5}}}}))
        assert(chronicle.call('skills.unlock',{tree='combat',id='focus'}))
        assert(chronicle.call('economy.add',{amount=1}))
    end})");Start();runtime.Tick({1,2,0,true},{},{},{},[&](const auto &,int,std::string &error){EXPECT_EQ(runtime.ApplySkillDamage(10),20);EXPECT_EQ(runtime.ApplySkillDamage(10,true),5);error="reject";return false;});
    EXPECT_FALSE(runtime.Diagnostics().empty());EXPECT_EQ(runtime.ApplySkillDamage(10),10);EXPECT_FALSE(runtime.HasSkillDamageEffects());EXPECT_FALSE(runtime.HasSkillDamageEffects(true));EXPECT_EQ(runtime.ApplySkillDamage(10,true),10);
}
TEST_F(ModSkillsRuntime, NativeAndScriptedEnemyDamageUseBonusesWithoutChangingStoredStats) {
    Add("test.mod",R"(return {on_tick=function()
        assert(chronicle.call('skills.register',{id='combat',title='Combat',description='Training',nodes={
          {id='focus',title='Focus',description='Focus',max_rank=2,effects={enemy_damage_per_rank=0.25}}}}))
        assert(chronicle.call('skills.set_rank',{tree='combat',id='focus',rank=2}))
    end})");Start();const int previousMode=mode;auto unit=std::make_unique<CMonstorUnit>();
    struct Cleanup{int old;~Cleanup(){mods::Scripts().Start(mods::Manager{});mods::Scripts().ResetState();mods::OwnedCombatDamage()={};mods::StopTrackingEntities();mode=old;}}cleanup{previousMode};
    mode=GAME_MODE_DUNGEON;mods::Scripts().Start(manager);mods::Scripts().Tick({2,0,0,true,0,1});ASSERT_TRUE(mods::Scripts().Diagnostics().empty());
    unit->CleanViewMonstor(0);auto &actor=unit->monster[0];actor.state=2;actor.hp=actor.max_hp=50;actor.revealed=-1;
    EXPECT_FLOAT_EQ(mods::ModifyOutgoingDamage(unit.get(),0,0,10),15);EXPECT_EQ(actor.hp,50);
    mods::OwnedCombatDamage().epoch=mods::Scripts().Epoch();std::string error;
    ASSERT_TRUE(mods::OwnedCombatDamage().outgoing.Set("test.mod",{{"id","double"},{"operation","multiply"},{"value",2}},error));
    EXPECT_FLOAT_EQ(mods::ModifyOutgoingDamage(unit.get(),0,0,10),30);
    const auto id=std::to_string(mods::TrackEntities(*unit).Get(0));mods::EntityTransaction transaction;
    ASSERT_TRUE(transaction.Stage(unit.get(),{{"combat.damage",{{"entity",id},{"amount",10}},"test.mod"}},error))<<error;EXPECT_EQ(actor.hp,50);
    transaction.Commit();EXPECT_EQ(actor.hp,35);EXPECT_EQ(actor.last_hit_damage,15);
    EXPECT_EQ(mods::TakeEntityReaction(unit.get(),0),MONSTER_DMG_HIT);EXPECT_EQ(actor.max_hp,50);
    mode=GAME_MODE_EDIT;EXPECT_FLOAT_EQ(mods::ModifyOutgoingDamage(unit.get(),0,0,10),10);
}

TEST(ModSkills, DefenseEffectsValidateIndependentlyAndClampAtFullReduction) {
    auto tree=Tree();tree["nodes"][0]["effects"]={{"incoming_damage_reduction_per_rank",0.75},{"enemy_damage_per_rank",0.25}};
    Json state=Json::object();mods::CallOwnedSkill(state,"skills.register",tree);
    EXPECT_TRUE(mods::SkillDamageFactors(state,true).empty());
    mods::CallOwnedSkill(state,"skills.set_rank",{{"tree","combat"},{"id","focus"},{"rank",2}});
    EXPECT_DOUBLE_EQ(mods::SkillDamageFactors(state,true).at({"combat","focus"}),0);
    EXPECT_DOUBLE_EQ(mods::SkillDamageFactors(state).at({"combat","focus"}),1.5);
    for(const Json &value:std::vector<Json>{-0.1,1.1,true,"0.1",std::numeric_limits<double>::infinity()}) {
        auto bad=tree;bad["nodes"][0]["effects"]["incoming_damage_reduction_per_rank"]=value;
        EXPECT_FALSE(mods::ValidateSkillOperation("skills.register",bad).empty());
    }
}
TEST_F(ModSkillsRuntime, DefensiveRanksStackRestoreAndDisappearWhenStopped) {
    for(const auto id:{"test.alpha","test.beta"})Add(id,R"(return {on_game_enter=function()
        assert(chronicle.call('skills.register',{id='combat',title='Combat',description='Defense',nodes={
          {id='guard',title='Guard',description='Reduce deductions',max_rank=2,effects={incoming_damage_reduction_per_rank=0.25}}}}))
        assert(chronicle.call('skills.set_rank',{tree='combat',id='guard',rank=2}))
    end})");Start();EXPECT_FALSE(runtime.HasSkillDamageEffects(true));runtime.Tick({2,0,0,true,0,1});
    ASSERT_TRUE(runtime.Diagnostics().empty());EXPECT_FLOAT_EQ(runtime.ApplySkillDamage(100,true),25);
    EXPECT_FALSE(runtime.HasSkillDamageEffects());EXPECT_FLOAT_EQ(runtime.ApplySkillDamage(100),100);
    EXPECT_NE(runtime.Skills()[0].description.find("Current reduction: 50%"),std::string::npos);
    for(float value:{0.0f,-0.0f,-1.0f,std::numeric_limits<float>::infinity()})
        EXPECT_EQ(std::bit_cast<unsigned>(runtime.ApplySkillDamage(value,true)),std::bit_cast<unsigned>(value));
    const auto saved=runtime.SaveState();runtime.ResetState();EXPECT_FALSE(runtime.HasSkillDamageEffects(true));
    std::string error;ASSERT_TRUE(runtime.LoadState(saved,error));EXPECT_FLOAT_EQ(runtime.ApplySkillDamage(100,true),25);
    runtime.Start(mods::Manager{});EXPECT_FALSE(runtime.HasSkillDamageEffects(true));EXPECT_EQ(runtime.ApplySkillDamage(100,true),100);
}
TEST_F(ModSkillsRuntime, DefenseUsesNativeLifeHookPreservesHealingAndDormantBehavior) {
    Add("test.mod",R"(return {on_game_enter=function()
        assert(chronicle.call('skills.register',{id='combat',title='Combat',description='Defense',nodes={
          {id='guard',title='Guard',description='Reduce deductions',max_rank=2,effects={incoming_damage_reduction_per_rank=0.25}}}}))
        assert(chronicle.call('skills.set_rank',{tree='combat',id='guard',rank=2}))
    end})");Start();auto status=std::make_unique<CUserStatus>();
    struct Cleanup{int oldMode;CUserStatus *oldStatus;~Cleanup(){mods::Scripts().Start(mods::Manager{});mods::Scripts().ResetState();mods::OwnedCombatDamage()={};mode=oldMode;UserStatus=oldStatus;}}cleanup{mode,UserStatus};
    mode=GAME_MODE_DUNGEON;UserStatus=status.get();status->hp[0]=100;status->max_hp[0]=100;status->life_step[0]=0;
    mods::Scripts().Start(manager);mods::Scripts().Tick({2,0,0,true,0,1});ASSERT_TRUE(mods::Scripts().Diagnostics().empty());
    status->AddNowLife(0,-20,0);EXPECT_EQ(status->hp[0],90);
    status->AddNowLife(0,10,0);EXPECT_EQ(status->hp[0],100);
    mods::OwnedCombatDamage().epoch=mods::Scripts().Epoch();std::string error;
    ASSERT_TRUE(mods::OwnedCombatDamage().incoming.Set("test.mod",{{"id","double"},{"operation","multiply"},{"value",2}},error));
    status->AddNowLife(0,-20,0);EXPECT_EQ(status->hp[0],80);
    mode=GAME_MODE_EDIT;status->AddNowLife(0,-20,0);EXPECT_EQ(status->hp[0],60);
    mode=GAME_MODE_DUNGEON;mods::Scripts().Start(mods::Manager{});status->AddNowLife(0,-20,0);EXPECT_EQ(status->hp[0],40);
}

TEST_F(ModSkillsRuntime, FullDefenseDoesNotSettlePendingLifeInterpolation) {
    Add("test.mod",R"(return {on_game_enter=function()
        assert(chronicle.call('skills.register',{id='guard',title='Guard',description='Full reduction',nodes={
          {id='guard',title='Guard',description='Block deductions',max_rank=1,effects={incoming_damage_reduction_per_rank=1}}}}))
        assert(chronicle.call('skills.unlock',{tree='guard',id='guard'}))
    end})");Start();auto status=std::make_unique<CUserStatus>();
    struct Cleanup{int oldMode;CUserStatus *oldStatus;~Cleanup(){mods::Scripts().Start(mods::Manager{});mods::Scripts().ResetState();mode=oldMode;UserStatus=oldStatus;}}cleanup{mode,UserStatus};
    mode=GAME_MODE_DUNGEON;UserStatus=status.get();mods::Scripts().Start(manager);mods::Scripts().Tick({2,0,0,true,0,1});
    status->hp[0]=100;status->max_hp[0]=100;status->next_hp[0]=80;status->life_step[0]=-2;
    status->AddNowLife(0,-10,10);EXPECT_EQ(status->hp[0],100);EXPECT_EQ(status->next_hp[0],80);EXPECT_EQ(status->life_step[0],-2);
    CUserStatus unrelated{};unrelated.hp[0]=100;unrelated.max_hp[0]=100;
    unrelated.AddNowLife(0,-10,0);EXPECT_EQ(unrelated.hp[0],90);
    EXPECT_TRUE(mods::Scripts().Diagnostics().empty());
}

TEST_F(ModSkillsRuntime, ShippedTrainingIsExplicitSavedAndResetsEveryDependentRank) {
    const auto source=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path()/"examples/mods/combat-training";
    std::filesystem::create_directories(root/"training");
    for(const auto *file:{"mod.json","main.lua"})std::filesystem::copy_file(source/file,root/"training"/file);
    Start();EXPECT_FALSE(runtime.HasGameplay());
    enabled["example.combat-training"]=true;Start();const mods::World world{2,0,0,true,0,1};runtime.Tick(world);
    ASSERT_TRUE(runtime.Diagnostics().empty());ASSERT_EQ(runtime.Skills().size(),2u);
    EXPECT_FALSE(runtime.HasSkillDamageEffects());EXPECT_FALSE(runtime.HasSkillDamageEffects(true));
    std::string error;ASSERT_TRUE(runtime.QueueSkillTraining("example.combat-training","combat","guard",error));
    runtime.Tick(world);EXPECT_FALSE(runtime.HasSkillDamageEffects(true));
    ASSERT_TRUE(runtime.QueueSkillTraining("example.combat-training","combat","focus",error))<<error;
    EXPECT_FALSE(runtime.HasSkillDamageEffects());runtime.Tick(world);EXPECT_FLOAT_EQ(runtime.ApplySkillDamage(100),110);
    ASSERT_TRUE(runtime.QueueSkillTraining("example.combat-training","combat","guard",error))<<error;
    runtime.Tick(world);EXPECT_FLOAT_EQ(runtime.ApplySkillDamage(100,true),95);
    const auto trained=runtime.SaveState();runtime.ResetState();ASSERT_TRUE(runtime.LoadState(trained,error));runtime.Tick(world);
    EXPECT_FLOAT_EQ(runtime.ApplySkillDamage(100),110);EXPECT_FLOAT_EQ(runtime.ApplySkillDamage(100,true),95);
    ASSERT_TRUE(runtime.QueueAction("example.combat-training","reset",error))<<error;runtime.Tick(world);
    EXPECT_FALSE(runtime.HasSkillDamageEffects());EXPECT_FALSE(runtime.HasSkillDamageEffects(true));
    for(const auto &skill:runtime.Skills())EXPECT_EQ(skill.rank,0);
    ASSERT_TRUE(runtime.QueueSkillTraining("example.combat-training","combat","guard",error));
    runtime.Tick(world);EXPECT_FALSE(runtime.HasSkillDamageEffects(true));
    EXPECT_TRUE(runtime.Diagnostics().empty());
}

}
