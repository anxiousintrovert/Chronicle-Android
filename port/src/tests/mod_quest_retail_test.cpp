#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include <fstream>
#include "gfx_fixture.hpp"
#include "gameloop.hpp"
#include "dun/gameloop.hpp"
#include "monstorunit.hpp"
#include "savedata.hpp"
#include "mods/party.hpp"
#include "mods/entity_events.hpp"
#include "mods/runtime.hpp"
#include "platform/clock.hpp"
#include "platform/config.hpp"
#include "platform/input_script.hpp"
#include "platform/paths.hpp"
namespace {
using Json=nlohmann::json;
bool produced=false,verified=false;int money_before=0;std::uint64_t produced_frame=0;
void QuestPump() {
    WindowPollEvents();InputPoll();InputScriptApply(GameFrameCount());
    if(::testing::Test::HasFatalFailure()){GameRequestStop();return;}
    if(!produced) {
        if(!mods::ReadPartySwitchState().value("ready",false)||!NowMonstorUnit||!mods::EnemyDeaths().Enabled())return;
        ASSERT_EQ(mods::Scripts().Quests().size(),1u);
        InputScriptInstall({});money_before=SaveData->GetDngStatus()->money;
        const int previous=NowMonstorUnit->current_monster;int deaths=0;
        for(unsigned slot=0;slot<NowMonstorUnit->SlotLimit()&&deaths<2;++slot) {
            auto &actor=NowMonstorUnit->monster[slot];
            if(actor.state==-1||actor.hp<=0||actor.max_hp<10)continue;
            // Test-only preparation. Native CheckDmg emits the events; no queue injection or Lua damage call.
            actor.hp=1;actor.poison_timer=1;NowMonstorUnit->current_monster=static_cast<int>(slot);
            const auto result=NowMonstorUnit->CheckDmg();EXPECT_EQ(result,MONSTER_DMG_KILLED);EXPECT_EQ(actor.hp,0);++deaths;
        }
        NowMonstorUnit->current_monster=previous;ASSERT_EQ(deaths,2);
        ASSERT_EQ(mods::EnemyDeaths().Pending().size(),2u);
        for(const auto &death:mods::EnemyDeaths().Pending())EXPECT_FALSE(death.scripted);
        EXPECT_EQ(mods::Scripts().Quests()[0].progress,0); // Delivery is deferred to the normal game tick.
        produced=true;produced_frame=GameFrameCount();return;
    }
    const auto quests=mods::Scripts().Quests();
    if(quests.size()==1&&quests[0].progress==2&&GameFrameCount()>produced_frame+20) {
        EXPECT_EQ(SaveData->GetDngStatus()->money,money_before+50);
        const auto state=Json::parse(mods::Scripts().SaveState());
        EXPECT_EQ(state["mods"]["audit.quest"]["quest_rewarded"]["poison"],true);
        EXPECT_FALSE(mods::Scripts().WantsEnemyDeaths());EXPECT_FALSE(mods::EnemyDeaths().Enabled());
        verified=true;GameRequestStop();
    }
}
TEST(ModQuestRetail, NativePoisonDeathsDeliverDeferredQuestReward) {
    const char *data=std::getenv("DC_DATA"),*audit=std::getenv("DC_QUEST_RETAIL_AUDIT");
    if(!data||!audit)GTEST_SKIP()<<"Run alone with DC_DATA and a new DC_QUEST_RETAIL_AUDIT directory";
    namespace fs=std::filesystem;const fs::path root=audit;ASSERT_FALSE(fs::exists(root));
    fs::create_directories(root/"mods/audit.quest");
    std::ofstream(root/"mods-profile.json")<<R"({"schema_version":1,"enabled":{"audit.quest":true}})";
    std::ofstream(root/"mods/audit.quest/mod.json")<<R"({"schema_version":1,"api_version":1,"id":"audit.quest","name":"Native quest event audit","version":"1","game":"chronicle","files":{},"script":"main.lua","permissions":["game.read","game.write","economy.write"],"requires_capabilities":["quests.dynamic.v1","quests.events.v1","quests.rewards.v1"]})";
    std::ofstream(root/"mods/audit.quest/main.lua")<<R"(return {on_game_enter=function()
        assert(chronicle.call('quests.register',{id='poison',title='Native deaths',description='Native poison path probe',
          objectives={{id='kills',title='Defeat enemies',goal=2,event='enemy_killed',match={source='native'}}},
          rewards={{operation='economy.add',arguments={amount=50}}}}))
    end})";
    produced=verified=false;produced_frame=0;
    PathsSetDataRoot(data);PathsSetSaveRoot(root);ConfigLoad();
    dc::test::GfxFixture graphics(640,480,1.0f,dc::test::GfxOptions{.offscreen=true});InputInit();
    InputScript script;std::string input="0\n",error;for(int i=90;i<18100;i+=100)input+=std::to_string(i)+" cross\n"+std::to_string(i+5)+"\n";
    ASSERT_TRUE(InputScriptParse(input,script,error))<<error;InputScriptInstall(std::move(script));
    ClockSetUnbounded(true);ClockReset();ClockAddPumpHook(QuestPump);
    ASSERT_TRUE(GameSetJump("dungeon:0"));GameSetFastLoad(true);GameSetAutosave(false);GameSetFrameBudget(18500);
    const auto result=RunGame(0,nullptr);ClockRemovePumpHook(QuestPump);InputShutdown();
    EXPECT_EQ(result,0);EXPECT_TRUE(produced);EXPECT_TRUE(verified);EXPECT_TRUE(mods::Scripts().Diagnostics().empty());
    std::ofstream(root/"result.json")<<Json{{"produced",produced},{"verified",verified},{"frame",produced_frame},{"money_before",money_before},{"money_after",SaveData->GetDngStatus()->money}}.dump(2);
}
}
