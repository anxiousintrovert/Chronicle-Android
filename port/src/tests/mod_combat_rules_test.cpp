#include <gtest/gtest.h>
#include "mods/combat_rules.hpp"
#include "mods/api.hpp"
using Json = nlohmann::json;
namespace {
mods::GameCommand Set(const char *owner, const char *id, double value=2, const char *direction="outgoing") {
    return {"combat.rule_set",{{"direction",direction},{"rule",{{"id",id},{"operation","multiply"},{"value",value}}}},owner};
}
TEST(ModCombatRules, AtomicBatchRollbackOwnershipAndEpochIsolation) {
    mods::CombatDamage state; std::string error;
    ASSERT_TRUE(mods::ApplyCombatRules(state,{Set("alpha","same"),Set("beta","same",3)},0,7,error)) << error;
    EXPECT_EQ(state.outgoing.Apply(10,{}),60);
    const auto before=mods::ReadCombatRules(state,7);
    EXPECT_FALSE(mods::ApplyCombatRules(state,{Set("alpha","same",4),{"economy.money_add",{{"amount",1}},"alpha"}},0,7,error));
    EXPECT_EQ(mods::ReadCombatRules(state,7),before);
    EXPECT_FALSE(mods::ApplyCombatRules(state,{Set("alpha","same",4)},1,7,error));
    EXPECT_EQ(mods::ReadCombatRules(state,7),before);
    ASSERT_TRUE(mods::ApplyCombatRules(state,{{"combat.rule_remove",{{"direction","outgoing"},{"id","same"}},"alpha"}},0,7,error));
    EXPECT_EQ(state.outgoing.Apply(10,{}),30);
    EXPECT_TRUE(mods::ReadCombatRules(state,8)["outgoing"].empty());
    ASSERT_TRUE(mods::ApplyCombatRules(state,{Set("alpha","new",4,"incoming")},0,8,error));
    EXPECT_TRUE(state.outgoing.Empty()); EXPECT_EQ(state.incoming.Apply(10,{}),40);
}
TEST(ModCombatRules, CapacityFailureDoesNotPublishPartialBatchOrConsumeRandomState) {
    mods::CombatDamage state; std::string error;
    auto random=Set("alpha","random"); random.arguments["rule"]={{"id","random"},{"operation","random_multiply"},{"min_value",1},{"max_value",2},{"seed",7}};
    ASSERT_TRUE(mods::ApplyCombatRules(state,{random},0,1,error)); state.outgoing.Apply(10,{});
    const auto before=mods::ReadCombatRules(state,1);
    std::vector<mods::GameCommand> commands;
    for(int i=0;i<32;++i) { auto id=std::to_string(i); commands.push_back(Set("alpha",id.c_str())); }
    EXPECT_FALSE(mods::ApplyCombatRules(state,commands,0,1,error)); EXPECT_EQ(mods::ReadCombatRules(state,1),before);
    ASSERT_TRUE(mods::ApplyCombatRules(state,{random},0,1,error)); EXPECT_EQ(mods::ReadCombatRules(state,1),before);
    auto detached=mods::ReadCombatRules(state,1); detached["outgoing"]=nullptr; EXPECT_EQ(mods::ReadCombatRules(state,1),before);
}
TEST(ModCombatRules, StrictPublicSchemaRejectsUnknownFieldsOwnersAndImpossibleSelectors) {
    const auto set=Set("alpha","test");
    EXPECT_TRUE(mods::ValidateOperation(set.operation,set.arguments).empty());
    EXPECT_TRUE(mods::ValidateOperation("combat.rules_get",Json::object()).empty());
    for(auto args : {Json{{"direction","bad"},{"rule",set.arguments["rule"]}}, Json{{"direction","incoming"},{"rule",{{"id","x"},{"operation","set"},{"value",1},{"native_model_id",1}}}}, Json{{"direction","outgoing"},{"rule",set.arguments["rule"]},{"owner","other"}}})
        EXPECT_FALSE(mods::ValidateOperation("combat.rule_set",args).empty());
    auto bad=set.arguments; bad["rule"]["character_id"]=6; EXPECT_FALSE(mods::ValidateOperation("combat.rule_set",bad).empty());
    bad=set.arguments; bad["rule"]["native_model_id"]=167; EXPECT_FALSE(mods::ValidateOperation("combat.rule_set",bad).empty());
    mods::CombatDamage state; std::string error;
    EXPECT_FALSE(mods::ApplyCombatRules(state,{{"combat.rule_remove",{{"direction","incoming"},{"id","test"}},""}},0,1,error));
    EXPECT_TRUE(state.Empty());
}
}
