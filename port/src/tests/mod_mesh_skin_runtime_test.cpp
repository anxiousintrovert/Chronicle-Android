#include <gtest/gtest.h>
#include "mods/mesh_skin_runtime.hpp"
#include "draw3d.hpp"
#include "framevu1.hpp"
#include "gameutil.hpp"
#include "mdt.hpp"
#include "visual.hpp"
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
namespace {
using Point=mods::MeshSkinBinding::Point;
struct Matrix { float v[4][4]{}; Matrix(){for(unsigned i=0;i<4;++i)v[i][i]=1;} };
class ModMeshSkinRuntime:public testing::Test {
protected:
    std::array<unsigned,48> blocks{};int model=0,rest=0,other=0;std::string error;
    const Point source[2]{{0,0,0},{100,0,0}},replacement[1]{{1,0,0}};
    void TearDown() override { for(unsigned i=0;i<blocks.size();++i)mods::DetachMeshSkin(&blocks[i]); Draw3DRetireVisuals(blocks.data(),sizeof(blocks)); }
    void Attach(unsigned index=0) {ASSERT_TRUE(mods::AttachMeshSkin(&blocks[index],&model,source,replacement,error))<<error;}
};
TEST_F(ModMeshSkinRuntime, RequiresBeginAndRejectsRestReuseUntilExplicitRebinding) {
    Attach();Matrix base,bone,inverse;bone.v[3][0]=8;
    mods::ApplyNativeMeshBone(0,base.v,bone.v,inverse.v,1);EXPECT_TRUE(mods::ReadMeshSkin(blocks.data()).empty());
    mods::BeginNativeMeshSkin(&model,&rest,2);mods::ApplyNativeMeshBone(0,base.v,bone.v,inverse.v,0.25f);
    ASSERT_EQ(mods::ReadMeshSkin(blocks.data()).size(),1u);EXPECT_EQ(mods::ReadMeshSkin(blocks.data())[0][0],3);
    mods::BeginNativeMeshSkin(&other,&rest,2);mods::ApplyNativeMeshBone(0,base.v,bone.v,inverse.v,1);EXPECT_EQ(mods::ReadMeshSkin(blocks.data())[0][0],3);
    mods::BeginNativeMeshSkin(&model,&other,2);EXPECT_TRUE(mods::ReadMeshSkin(blocks.data()).empty());EXPECT_FALSE(mods::MeshSkinError(blocks.data()).empty());
    mods::BeginNativeMeshSkin(&model,&rest,2);EXPECT_TRUE(mods::ReadMeshSkin(blocks.data()).empty());
    Attach();mods::BeginNativeMeshSkin(&model,&other,2);EXPECT_EQ(mods::ReadMeshSkin(blocks.data())[0][0],1);
}
TEST_F(ModMeshSkinRuntime, FailedAttachPreservesBindingAndInvalidInfluenceHidesPartialPose) {
    Attach();mods::BeginNativeMeshSkin(&model,&rest,2);
    EXPECT_FALSE(mods::AttachMeshSkin(blocks.data(),&model,{},replacement,error));
    EXPECT_EQ(mods::ReadMeshSkin(blocks.data())[0][0],1);
    Matrix base,bone,inverse;bone.v[3][0]=8;mods::ApplyNativeMeshBone(0,base.v,bone.v,inverse.v,0.5f);
    EXPECT_EQ(mods::ReadMeshSkin(blocks.data())[0][0],5);
    mods::ApplyNativeMeshBone(2,base.v,bone.v,inverse.v,1);
    EXPECT_TRUE(mods::ReadMeshSkin(blocks.data()).empty());EXPECT_FALSE(mods::MeshSkinError(blocks.data()).empty());
}
TEST_F(ModMeshSkinRuntime, VisualRebuildArenaRetirementAndSweepReleaseOnlyTheirBindings) {
    Draw3DRegisterVisual(blocks.data(),false);Draw3DRegisterVisual(blocks.data()+16,true);Draw3DRegisterVisual(blocks.data()+32,false);
    Attach(0);Attach(16);Attach(32);ASSERT_EQ(mods::MeshSkinBindingCount(),3u);
    Draw3DRegisterVisual(blocks.data(),false);EXPECT_EQ(mods::MeshSkinBindingCount(),2u);
    Draw3DSweepVisuals();EXPECT_EQ(mods::MeshSkinBindingCount(),1u);
    Draw3DRetireVisuals(blocks.data()+32,16*sizeof(unsigned));EXPECT_EQ(mods::MeshSkinBindingCount(),0u);
}
TEST_F(ModMeshSkinRuntime, NativeMotionProcDrivesBoundPoseWithoutChangingNativeResults) {
    struct alignas(16) Model {MDT_HEADER header{};alignas(16) float vertices[2][4]{};} native;
    native.header.vertex_num=2;native.header.vertex_ofs=offsetof(Model,vertices);
    alignas(16) float original[2][4]{{0,0,0,1},{100,0,0,1}};
    CVisualMDTVu1 visual;visual.data=reinterpret_cast<u_int*>(&native);
    CFrameVu1 frames[2];frames[0].visual=&visual;
    tagFRAME_INF info[2]{};info[0].parent_frame=0;info[1].parent_frame=0;info[0].vertex_count=2;info[0].base_vertices=original;
    sceVu0UnitMatrix(info[0].matrix);sceVu0UnitMatrix(frames[1].local);frames[1].local[3][0]=8;
    sceVu0FMATRIX bind[2];sceVu0UnitMatrix(bind[0]);sceVu0UnitMatrix(bind[1]);tagMOTION_TYPE motion{};motion.base_matrices=bind;
    Mot_Key key{};key.frame=0;key.value[0]=25;
    Mot_List child{};child.frame=0;child.target=1;child.key_count=1;child.keys=&key;
    Mot_List root{};root.frame=0;root.target=0;root.next=&child;
    ASSERT_EQ(MotionProc2(frames,&motion,info,&root),&child);MotionProc2(frames,&motion,info,&child);
    const auto expected=native.vertices[0][0];EXPECT_EQ(expected,2);
    ASSERT_TRUE(mods::AttachMeshSkin(blocks.data(),&native,source,replacement,error))<<error;
    MotionProc2(frames,&motion,info,&root);MotionProc2(frames,&motion,info,&child);
    EXPECT_EQ(native.vertices[0][0],expected);ASSERT_EQ(mods::ReadMeshSkin(blocks.data()).size(),1u);EXPECT_EQ(mods::ReadMeshSkin(blocks.data())[0][0],3);
    MotionProc2(frames,&motion,info,&root);MotionProc2(frames,&motion,info,&child);
    EXPECT_EQ(mods::ReadMeshSkin(blocks.data())[0][0],3);EXPECT_EQ(original[0][0],0);EXPECT_EQ(original[1][0],100);
}
}
