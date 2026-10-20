#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include "gfx_fixture.hpp"
#include "character.hpp"
#include "framevu1.hpp"
#include "visual.hpp"
#include "mdt.hpp"
#include "gameloop.hpp"
#include "dun/gameloop.hpp"
#include "gfx/gfx.hpp"
#include "mods/party.hpp"
#include "mods/session.hpp"
#include "mods/mesh_discovery.hpp"
#include "mods/mesh_visual_runtime.hpp"
#include "mods/mesh_skin_runtime.hpp"
#include "platform/clock.hpp"
#include "platform/config.hpp"
#include "platform/input_script.hpp"
#include "platform/paths.hpp"
#include "chronicle/random_state.hpp"
#include <fstream>
#include <set>
#include <cmath>
#include <cstring>
namespace {
namespace fs=std::filesystem;using Json=nlohmann::json;
fs::path output;
CVisualMDTVu1 *model_visual=nullptr;
MDT_HEADER *model=nullptr;
tagFRAME_INF *binding=nullptr;
std::vector<const void *> blocks;
std::vector<mods::MeshSkinBinding::Point> first_pose;
std::uint64_t stage_start=0;
int stage=0;bool animation_changed=false,completed=false;float maximum_delta=0;

void Screenshot(const char *name) {
    std::vector<std::uint8_t> pixels;std::uint32_t width=0,height=0;
    ASSERT_TRUE(GameScreenshot(pixels,width,height));ASSERT_TRUE(gfx::WritePng(output/name,pixels.data(),width,height));
}
void RebuildFromRest() {
    auto *positions=reinterpret_cast<float (*)[4]>(reinterpret_cast<unsigned char *>(model)+model->vertex_ofs);
    std::vector<float> saved(model->vertex_num*4);std::memcpy(saved.data(),positions,saved.size()*sizeof(float));
    std::memcpy(positions,binding->base_vertices,saved.size()*sizeof(float));
    const auto rng=chronicle::RandomState();
    for(const auto *block:blocks)model_visual->CreateVUdataFromMDT(static_cast<unsigned *>(const_cast<void *>(block)),reinterpret_cast<unsigned *>(model),0,0);
    std::memcpy(positions,saved.data(),saved.size()*sizeof(float));
    for(const auto *block:blocks)model_visual->CreateVUdataFromMDTRemake(static_cast<unsigned *>(const_cast<void *>(block)),reinterpret_cast<unsigned *>(model),1);
    EXPECT_EQ(chronicle::RandomState(),rng);EXPECT_EQ(std::memcmp(saved.data(),positions,saved.size()*sizeof(float)),0);
}
void SelectSkinnedPlayerModel() {
    auto *frames=reinterpret_cast<CFrameVu1 *>(CharaMain.frame);
    for(auto *motion:CharaMain.motion) {
        if(!motion||!motion->frame_info)continue;
        for(auto *list=motion->proc_list2;list;list=list->next) {
            if(list->type==MOTION_KEY_SKIP)continue;
            auto &info=motion->frame_info[list->frame];
            if(!info.base_vertices||info.vertex_count<3||info.vertex_count>3000)continue;
            auto *visual=dynamic_cast<CVisualMDTVu1 *>(frames[list->frame].GetVisual());
            if(!visual||!visual->data)continue;
            auto *header=reinterpret_cast<MDT_HEADER *>(visual->data);
            if(header->vertex_num!=int(info.vertex_count))continue;
            if(!model||header->vertex_num>model->vertex_num){model_visual=visual;model=header;binding=&info;}
        }
    }
    ASSERT_NE(model,nullptr) << "player has no skinned MDT visual";
    std::set<const void *> distinct;
    for(auto *block:model_visual->vu_data_buffer)if(block&&Draw3DFindVisual(block))distinct.insert(block);
    blocks.assign(distinct.begin(),distinct.end());ASSERT_FALSE(blocks.empty());
}
void PreparePackage() {
    SelectSkinnedPlayerModel();ASSERT_NE(model,nullptr);ASSERT_FALSE(blocks.empty());
    std::string error;ASSERT_TRUE(mods::MeshDiscoveryRecorder().Start(output/"reference",error))<<error;
    RebuildFromRest();mods::MeshDiscoveryRecorder().Stop();
    ASSERT_TRUE(mods::MeshDiscoveryRecorder().Errors().empty());ASSERT_EQ(mods::MeshDiscoveryRecorder().Count(),1u);
    auto path=fs::directory_iterator(output/"reference")->path();
#ifdef _WIN32
    path=fs::path(std::wstring(LR"(\\?\)")+path.make_preferred().native());
#endif
    std::ifstream input(path);Json snapshot;input>>snapshot;
    ASSERT_FALSE(snapshot["textures"].empty());
    auto asset=snapshot["mesh"];
    for(auto &vertex:asset["vertices"])vertex[0]=vertex[0].get<float>()*2.0f;
    fs::create_directories(output/"mods/audit.mesh");std::ofstream(output/"mods/audit.mesh/body.mesh.json")<<asset.dump();
    const Json manifest={{"schema_version",1},{"id","audit.mesh"},{"name","Retail mesh audit"},{"version","1.0.0"},{"game","chronicle"},
        {"files",Json::object()},{"requires_capabilities",{"assets.mesh_replace.v1"}},
        {"meshes",{{{"texture",snapshot["textures"][0]},{"native_vertices",model->vertex_num},{"source","body.mesh.json"}}}}};
    std::ofstream(output/"mods/audit.mesh/mod.json")<<manifest.dump();
    std::ofstream(output/"target.json")<<Json{{"native_vertices",model->vertex_num},{"textures",snapshot["textures"]},{"buffers",blocks.size()}}.dump(2);
    // The shared session discovers a real package, disabled by default.
    mods::Shared().Start({output/"mods"},{},output);mods::Scripts().Start(mods::Shared().Active());
    ASSERT_TRUE(mods::Scripts().ActiveOwners().empty());RebuildFromRest();
    for(const auto *block:blocks){const auto *source=Draw3DFindVisual(block);ASSERT_NE(source,nullptr);EXPECT_EQ(&mods::ResolveReplacementVisual(*source),source);}
    ASSERT_TRUE(mods::Shared().SetEnabled("audit.mesh",true));EXPECT_TRUE(mods::Shared().Pending());
    EXPECT_TRUE(mods::Scripts().ActiveOwners().empty());
    // Simulate the requested restart boundary while retaining the loaded native scene.
    mods::Shared().Start({output/"mods"},{},output);mods::Scripts().Start(mods::Shared().Active());
    ASSERT_EQ(mods::Scripts().ActiveOwners(),std::vector<std::string>{"audit.mesh"});RebuildFromRest();
    for(const auto *block:blocks){const auto *source=Draw3DFindVisual(block);ASSERT_NE(source,nullptr);EXPECT_NE(&mods::ResolveReplacementVisual(*source),source);}
}
void Pump() {
    WindowPollEvents();InputPoll();InputScriptApply(GameFrameCount());
    if(::testing::Test::HasFatalFailure()){GameRequestStop();return;}
    if(stage==0) {
        if(!mods::ReadPartySwitchState().value("ready",false))return;
        InputScript released;std::string error;ASSERT_TRUE(InputScriptParse("0\n",released,error));InputScriptInstall(std::move(released));
        stage=1;stage_start=GameFrameCount();return;
    }
    if(stage==1&&GameFrameCount()>=stage_start+20) {
        Screenshot("native.png");const auto rng=chronicle::RandomState();PreparePackage();EXPECT_EQ(chronicle::RandomState(),rng);if(::testing::Test::HasFatalFailure()){GameRequestStop();return;}ASSERT_FALSE(blocks.empty());
        stage=2;stage_start=GameFrameCount();
        InputScript attack;std::string error;
        const auto input="0\n"+std::to_string(stage_start+10)+" cross\n"+std::to_string(stage_start+15)+"\n"+std::to_string(stage_start+45)+" cross\n"+std::to_string(stage_start+50)+"\n";
        ASSERT_TRUE(InputScriptParse(input,attack,error));InputScriptInstall(std::move(attack));return;
    }
    if(stage==2) {
        ASSERT_TRUE(mods::Scripts().Diagnostics().empty());
        const auto *source=Draw3DFindVisual(blocks.front());ASSERT_NE(source,nullptr);
        const auto &replacement=mods::ResolveReplacementVisual(*source);ASSERT_NE(&replacement,source);
        if(replacement.vertices.size()==source->vertices.size())for(std::size_t i=0;i<replacement.vertices.size();++i)
            for(unsigned axis=0;axis<3;++axis)maximum_delta=std::max(maximum_delta,std::abs(replacement.vertices[i].position[axis]-source->vertices[i].position[axis]));
        const auto skin=mods::ReadMeshSkin(blocks.front());
        if(!skin.empty()) {
            if(first_pose.empty())for(const auto &v:replacement.vertices)first_pose.push_back({v.position[0],v.position[1],v.position[2]});
            else for(std::size_t i=0;i<replacement.vertices.size();++i)for(unsigned axis=0;axis<3;++axis) {
                const auto value=replacement.vertices[i].position[axis];EXPECT_TRUE(std::isfinite(value));
                if(std::abs(value-first_pose[i][axis])>0.001f)animation_changed=true;
            }
        }
        if(GameFrameCount()==stage_start+25)Screenshot("replacement.png");
        if(GameFrameCount()>=stage_start+90) {
            Screenshot("replacement-later.png");EXPECT_TRUE(animation_changed);EXPECT_FALSE(first_pose.empty());
            ASSERT_TRUE(mods::Shared().SetEnabled("audit.mesh",false));EXPECT_TRUE(mods::Shared().Pending());
            EXPECT_EQ(mods::Scripts().ActiveOwners().size(),1u);
            mods::Shared().Start({output/"mods"},{},output);mods::Scripts().Start(mods::Shared().Active());
            EXPECT_TRUE(mods::Scripts().ActiveOwners().empty());EXPECT_EQ(mods::MeshSkinBindingCount(),0u);
            for(const auto *block:blocks){const auto *native=Draw3DFindVisual(block);ASSERT_NE(native,nullptr);EXPECT_EQ(&mods::ResolveReplacementVisual(*native),native);}
            stage=3;stage_start=GameFrameCount();
        }
    }
    if(stage==3&&GameFrameCount()>=stage_start+30){Screenshot("restored.png");completed=true;GameRequestStop();stage=4;}
}
TEST(ModMeshRetail, SharedManagerReplacementAnimatesAndDisablesWithoutNativeMutation) {
    const char *data=std::getenv("DC_DATA"),*audit=std::getenv("DC_MESH_AUDIT");
    if(!data||!audit||!*audit)GTEST_SKIP()<<"Run alone with DC_DATA and DC_MESH_AUDIT (a disposable directory)";
    output=audit;fs::create_directories(output);PathsSetDataRoot(data);PathsSetSaveRoot(output);ConfigLoad();
    dc::test::GfxFixture graphics(640,480,1.0f,dc::test::GfxOptions{.offscreen=true});InputInit();
    InputScript script;std::string input="0\n",error;
    for(int i=90;i<18100;i+=100)input+=std::to_string(i)+" cross\n"+std::to_string(i+5)+"\n";
    ASSERT_TRUE(InputScriptParse(input,script,error));InputScriptInstall(std::move(script));
    ClockSetUnbounded(true);ClockReset();ClockAddPumpHook(Pump);ASSERT_TRUE(GameSetJump("dungeon:0"));
    GameSetFastLoad(true);GameSetAutosave(false);GameSetFrameBudget(18500);
    const int result=RunGame(0,nullptr);ClockRemovePumpHook(Pump);InputShutdown();mods::MeshDiscoveryRecorder().Stop();
    mods::Scripts().Start(mods::Manager{});
    std::ofstream(output/"audit.json")<<Json{{"completed",completed},{"animation_changed",animation_changed},{"maximum_vertex_delta",maximum_delta},
        {"frames",GameFrameCount()},{"stage",stage},{"bindings_after_disable",mods::MeshSkinBindingCount()}}.dump(2);
    EXPECT_GT(maximum_delta,0.1f);
    EXPECT_EQ(result,0);EXPECT_TRUE(completed)<<"stage="<<stage<<" frame="<<GameFrameCount()<<" "<<mods::ReadPartySwitchState().dump();EXPECT_TRUE(animation_changed);EXPECT_EQ(mods::MeshSkinBindingCount(),0u);
}
}
