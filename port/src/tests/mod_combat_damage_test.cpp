#include <gtest/gtest.h>
#include "mods/combat_damage.hpp"
#include "mods/model_identity.hpp"
#include "mods/runtime.hpp"
#include "userstatus.hpp"
#include "dun/gameloop.hpp"
#include "monstorunit.hpp"
#include "frame.hpp"
#include "main.hpp"
#include <memory>
#include <bit>
#include <cstdlib>
extern s32 mode;
namespace {
class ModCombatDamage : public testing::Test {
protected:
    int old_mode = mode;
    CUserStatus *old_status = UserStatus;
    std::unique_ptr<CUserStatus> status = std::make_unique<CUserStatus>();
    std::unique_ptr<CMonstorUnit> unit = std::make_unique<CMonstorUnit>();
    CFrame frame; char script = 0;
    void SetUp() override {
        mods::OwnedCombatDamage() = {}; mods::OwnedCombatDamage().epoch = mods::Scripts().Epoch();
        mode = GAME_MODE_DUNGEON; UserStatus = status.get();
        status->hp[0] = 100; status->max_hp[0] = 100; status->life_step[0] = 0;
        status->equipped_weapon_slot[0] = 0; status->chara_weapons[0][0].item_no = 7;
        unit->CleanViewMonstor(0); unit->model_count = 1;
        unit->script_data[0] = &script; unit->base_chara[0][0].frame = &frame;
        unit->monster[0].state = 1; unit->monster[0].hp = 20; unit->monster[0].max_hp = 100; unit->monster[0].base_model = 0;
        mods::RecordNativeModelLoad(unit.get(), 0, 27);
    }
    void TearDown() override {
        mods::ClearNativeModelIdentities(unit.get()); mods::OwnedCombatDamage() = {}; mode = old_mode; UserStatus = old_status;
    }
    void Rule(mods::DamageModifiers &rules, nlohmann::json extra = nlohmann::json::object()) {
        nlohmann::json definition = {{"id","test"},{"operation","multiply"},{"value",2}};
        definition.update(extra); std::string error;
        ASSERT_TRUE(rules.Set("test", definition, error)) << error;
    }
};
TEST_F(ModCombatDamage, DormantNativeLifeAndFloatPathsPreserveBehaviorAndRng) {
    srand(17); const int next = rand(); srand(17);
    for (float value : {0.0f,-0.0f,123.5f}) EXPECT_EQ(std::bit_cast<unsigned>(mods::ModifyOutgoingDamage(unit.get(),0,0,value)), std::bit_cast<unsigned>(value));
    status->AddNowLife(0,-10,0); EXPECT_EQ(status->hp[0],90);
    status->AddNowLife(0,-20,10); EXPECT_EQ(status->hp[0],90); EXPECT_EQ(status->next_hp[0],70); EXPECT_EQ(status->life_step[0],-2);
    EXPECT_EQ(rand(),next);
}
TEST_F(ModCombatDamage, OutgoingUsesExactModelCharacterWeaponAndTargetHealth) {
    Rule(mods::OwnedCombatDamage().outgoing, {{"native_model_id",27},{"character_id",0},{"weapon_id",7},{"target_hp_below_percent",30}});
    EXPECT_EQ(mods::ModifyOutgoingDamage(unit.get(),0,0,10),20);
    EXPECT_EQ(mods::ModifyOutgoingDamage(unit.get(),0,1,10),10);
    unit->monster[0].hp=30; EXPECT_EQ(mods::ModifyOutgoingDamage(unit.get(),0,0,10),10);
    unit->monster[0].hp=20; mods::ClearNativeModelIdentities(unit.get()); EXPECT_EQ(mods::ModifyOutgoingDamage(unit.get(),0,0,10),10);
}
TEST_F(ModCombatDamage, IncomingTransformsDeductionButNeverHealingAndUsesPendingHealth) {
    Rule(mods::OwnedCombatDamage().incoming, {{"character_id",0},{"weapon_id",7},{"target_hp_below_percent",50}});
    status->next_hp[0]=40; status->life_step[0]=-1;
    status->AddNowLife(0,-10,0); EXPECT_EQ(status->hp[0],20); EXPECT_EQ(status->life_step[0],0);
    status->AddNowLife(0,10,0); EXPECT_EQ(status->hp[0],30);
    CUserStatus unrelated{}; unrelated.hp[0]=100; unrelated.max_hp[0]=100; unrelated.AddNowLife(0,-10,0); EXPECT_EQ(unrelated.hp[0],90);
}
TEST_F(ModCombatDamage, ExplicitBlockDoesNotStartOrSettleAnInterpolation) {
    Rule(mods::OwnedCombatDamage().incoming, {{"value",0}});
    status->next_hp[0]=80; status->life_step[0]=-2;
    status->AddNowLife(0,-10,10);
    EXPECT_EQ(status->hp[0],100); EXPECT_EQ(status->next_hp[0],80); EXPECT_EQ(status->life_step[0],-2);
}
TEST_F(ModCombatDamage, StaleEpochNonDungeonAndInactiveOwnersCannotApplyRules) {
    Rule(mods::OwnedCombatDamage().outgoing); Rule(mods::OwnedCombatDamage().incoming);
    mode=GAME_MODE_EDIT; EXPECT_EQ(mods::ModifyOutgoingDamage(unit.get(),0,0,10),10);
    status->AddNowLife(0,-10,0); EXPECT_EQ(status->hp[0],90);
    mode=GAME_MODE_DUNGEON; ++mods::OwnedCombatDamage().epoch;
    EXPECT_EQ(mods::ModifyOutgoingDamage(unit.get(),0,0,10),10);
    mods::RetainCombatDamageOwners(); EXPECT_TRUE(mods::OwnedCombatDamage().Empty());
    mods::OwnedCombatDamage().epoch=mods::Scripts().Epoch(); Rule(mods::OwnedCombatDamage().incoming);
    mods::RetainCombatDamageOwners(); EXPECT_TRUE(mods::OwnedCombatDamage().Empty());
}
}
