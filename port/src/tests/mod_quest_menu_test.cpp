#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include "gfx_fixture.hpp"
#include "captionfont.hpp"
#include "gamepad.hpp"
#include "mods/menu.hpp"
#include "mods/runtime.hpp"
#include "mods/session.hpp"
#include "platform/input_script.hpp"
#include "platform/paths.hpp"
#include "platform/config.hpp"
#include "platform/clock.hpp"
#include "gameloop.hpp"

namespace {
TEST(ModQuestMenu, SharedFrameworkFiltersScrollsAndAcceptsControllerAndMouseWithoutWrites) {
    using Json=nlohmann::json;namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("chronicle-quest-menu-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup{fs::path root;~Cleanup(){mods::menu::Close();mods::Scripts().Start(mods::Manager{});mods::Scripts().ResetState();InputScriptInstall({});InputSetMenuMouse(false);InputShutdown();std::error_code error;fs::remove_all(root,error);}}cleanup{root};
    fs::create_directories(root/"mods/test.quests");
    Json quests=Json::array();
    for(int i=0;i<10;++i)quests.push_back({{"id","quest."+std::to_string(i)}, {"title",i==0?"Welcome home":"Objective "+std::to_string(i)},
        {"description",i==0?"Return to Norune and speak to the townspeople. This quest is complete.":"Explore the village and recover the missing treasures. Braces such as {color} must stay literal."},{"goal",3}});
    std::ofstream(root/"mods/test.quests/mod.json")<<Json{{"schema_version",1},{"id","test.quests"},{"name","Quest journal test"},{"version","1"},{"game","chronicle"},{"files",Json::object()},{"api_version",1},{"script","main.lua"}}.dump();
    std::ofstream(root/"mods/test.quests/main.lua")<<"return {}";
    std::ofstream(root/"mods-profile.json")<<Json{{"schema_version",1},{"enabled",{{"test.quests",true}}}}.dump();
    dc::test::GfxFixture graphics(640,480,1.0f,dc::test::GfxOptions{.offscreen=true});InputInit();
    if(const char *data=std::getenv("DC_DATA")) {
        PathsSetDataRoot(data);PathsSetSaveRoot(root);ConfigLoad();ClockSetUnbounded(true);ClockReset();
        ASSERT_TRUE(GameSetJump("dungeon:0"));GameSetFastLoad(true);GameSetAutosave(false);GameSetFrameBudget(180);
        ASSERT_EQ(RunGame(0,nullptr),0);
    }
    mods::Shared().Start({root/"mods"},{},root);mods::Scripts().Start(mods::Shared().Active());
    auto state=Json::parse(mods::Scripts().SaveState());state["mods"]["test.quests"]["quests"]={{"quest.0",3},{"quest.1",1}};
    for(auto definition:quests){const auto id=definition["id"].get<std::string>();definition.erase("id");state["mods"]["test.quests"]["quest_definitions"][id]=definition;}
    auto &first_definition=state["mods"]["test.quests"]["quest_definitions"]["quest.0"];
    first_definition.erase("goal");first_definition["objectives"]={{{"id","home"},{"title","Return to Norune"},{"goal",1}},{{"id","people"},{"title","Speak to villagers"},{"goal",2}}};
    state["mods"]["test.quests"]["quest_objectives"]["quest.0"]={{"home",1},{"people",2}};
    state["mods"]["test.quests"]["quest_definitions"]["quest.2"]["explicit_start"]=true;
    state["mods"]["test.quests"]["quest_definitions"]["quest.2"]["rewards"]={{{"operation","economy.add"},{"arguments",{{"amount",50}}}}};
    auto &skillTree=state["mods"]["test.quests"]["skill_trees"]["combat"];
    skillTree={{"title","Combat"},{"description","Training"},{"nodes",Json::array()}};
    skillTree["nodes"].push_back({{"id","focus"},{"title","Focus"},{"description","Build focus before learning Power."},{"max_rank",2},{"effects",{{"enemy_damage_per_rank",0.25}}}});
    skillTree["nodes"].push_back({{"id","power"},{"title","Power"},{"description","Improve your power."},{"max_rank",3},{"prerequisites",{{{"id","focus"},{"rank",2}}}}});
    state["mods"]["test.quests"]["skill_ranks"]["combat"]["focus"]=1;
    std::string error;ASSERT_TRUE(mods::Scripts().LoadState(state.dump(),error))<<error;
    const auto before=mods::Scripts().SaveState();
    InputSetMenuMouse(true);
    const auto font=fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path()/"tools/font/DarkCloudCompendium.ttf";
    ASSERT_TRUE(capfont::Load(font));
    const auto draw=[&](const char *name){graphics.Frame({0,0,0,128},[]{mods::menu::Draw();});
        if(const char *folder=std::getenv("DC_QUEST_MENU_AUDIT")){fs::create_directories(folder);EXPECT_TRUE(gfx::WritePng(fs::path(folder)/name,graphics.pixels.data(),graphics.width,graphics.height));}
        return graphics.pixels;};
    // Selection brackets pulse with the native UI clock (including x=42 and x=267); compare text/pane content.
    const auto same_content=[](const auto &a,const auto &b){
        for(int y=90;y<345;++y)for(int x=43;x<595;++x)if(x<267||x>=295)
            for(int c=0;c<4;++c)if(a[(y*640+x)*4+c]!=b[(y*640+x)*4+c])return false;
        return true;
    };
    mods::menu::Open();const auto manager=draw("manager.png");
    mods::menu::Run(PAD_L1);const auto all=draw("all.png");EXPECT_TRUE(all!=manager);
    mods::menu::Run(PAD_RIGHT);EXPECT_TRUE(draw("objectives-page.png")!=all);mods::menu::Run(PAD_LEFT);
    mods::menu::Run(PAD_DOWN);mods::menu::Run(PAD_DOWN);EXPECT_TRUE(draw("registered.png")!=all);
    mods::menu::Run(PAD_RIGHT);EXPECT_TRUE(draw("rewards.png")!=all);mods::menu::Run(PAD_LEFT);
    mods::menu::Run(PAD_UP);mods::menu::Run(PAD_UP);
    mods::menu::Run(PAD_SQUARE);const auto ongoing=draw("ongoing.png");EXPECT_TRUE(ongoing!=all);
    mods::menu::Run(PAD_SQUARE);const auto complete=draw("complete.png");EXPECT_TRUE(complete!=ongoing);
    mods::menu::Run(PAD_SQUARE);EXPECT_TRUE(same_content(draw("all-again.png"),all));
    for(int i=0;i<9;++i)mods::menu::Run(PAD_DOWN);EXPECT_TRUE(draw("scrolled.png")!=all);
    mods::menu::Run(PAD_CIRCLE);EXPECT_TRUE(mods::menu::IsOpen());EXPECT_TRUE(same_content(draw("returned.png"),manager));
    mods::menu::Run(PAD_L2);const auto skills=draw("skills.png");EXPECT_TRUE(skills!=manager);
    mods::menu::Run(PAD_RIGHT);EXPECT_TRUE(draw("skill-effects.png")!=skills);mods::menu::Run(PAD_LEFT);
    mods::menu::Run(PAD_DOWN);EXPECT_TRUE(draw("skill-locked.png")!=skills);
    mods::menu::Run(PAD_CIRCLE);EXPECT_TRUE(same_content(draw("skills-returned.png"),manager));
    // Pointer starts at 320,240; click the new Quests entry at 80,374.
    InputScript mouse;ASSERT_TRUE(InputScriptParse("0 mouse:-240,134 mouse1\n1\n",mouse,error))<<error;
    InputScriptInstall(std::move(mouse));InputScriptApply(0);mods::menu::Run(0);const auto clicked=draw("mouse.png");
    // Ignore the cursor below the content; the quest page itself must match controller entry.
    EXPECT_TRUE(same_content(all,clicked));
    InputScriptApply(1);mods::menu::Run(0);mods::menu::Run(PAD_CIRCLE);
    InputScript skillMouse;ASSERT_TRUE(InputScriptParse("0 mouse:440,-269 mouse1\n1 mouse:0,280\n",skillMouse,error))<<error;
    InputScriptInstall(std::move(skillMouse));InputScriptApply(0);mods::menu::Run(0);
    InputScriptApply(1);mods::menu::Run(0); // Move the cursor below the compared content.
    EXPECT_TRUE(same_content(skills,draw("skills-mouse.png")));
    mods::menu::Run(PAD_CROSS);EXPECT_TRUE(draw("skill-training-unavailable.png")!=skills);
    InputScriptApply(1);mods::menu::Run(0);mods::menu::Run(PAD_CIRCLE);mods::menu::Run(PAD_CIRCLE);EXPECT_FALSE(mods::menu::IsOpen());
    EXPECT_EQ(mods::Scripts().SaveState(),before);
}
}
