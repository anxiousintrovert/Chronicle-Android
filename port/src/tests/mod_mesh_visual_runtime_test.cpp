#include "tests/gfx_fixture.hpp"
#include <gtest/gtest.h>
#include "mods/mesh_visual_runtime.hpp"
#include "mods/mesh_skin_runtime.hpp"
#include <limits>
#include <memory>
namespace {
using Point=mods::MeshSkinBinding::Point;
struct Matrix {float v[4][4]{};Matrix(){for(unsigned i=0;i<4;++i)v[i][i]=1;}};
struct Registered {
    std::array<unsigned,16> block{};int model=0,rest=0;
    std::array<Point,3> points{{{-0.5f,-0.5f,0.5f},{0.5f,-0.5f,0.5f},{0,0.5f,0.5f}}};
    std::array<std::uint32_t,3> indices{0,1,2};
    Draw3DVisual &source;
    Registered():source(Draw3DRegisterVisual(block.data(),false)) {
        for(const auto &p:points)source.vertices.push_back({{p[0],p[1],p[2]},{0,0,1},{0,0},{255,0,0,255}});
        source.strips.resize(1);source.strips[0].index_count=3;source.indices={0,1,2};Draw3DFinishVisual(source);
    }
    ~Registered(){Draw3DRetireVisuals(block.data(),sizeof(block));}
    mods::MeshAsset Asset(float offset=0.25f)const {
        mods::MeshAsset asset;for(const auto &p:points)asset.vertices.push_back({p[0]+offset,p[1],p[2],0,0,1,0,0});asset.triangles.push_back({0,{0,1,2}});return asset;
    }
};
double RedCenter(dc::test::GfxFixture &graphics,const Draw3DVisual &source) {
    const auto &visual=mods::ResolveReplacementVisual(source);gfx::MeshConstants constants{};constants.flags=gfx::kMeshVertexColor;
    constants.mvp[0]=constants.mvp[5]=constants.mvp[10]=constants.mvp[15]=1;for(auto &value:constants.diffuse)value=1;
    graphics.Frame({0,0,0,128},[&]{gfx::DrawMesh(visual.mesh,0,3,constants,{},gfx::DrawState{});});
    double sum=0,count=0;for(unsigned y=0;y<graphics.height;++y)for(unsigned x=0;x<graphics.width;++x){const auto p=graphics.Pixel(x,y);if(p[0]>200&&p[1]<30&&p[2]<30){sum+=x;++count;}}
    EXPECT_GT(count,0) << "center=" << unsigned(graphics.Pixel(graphics.width/2,graphics.height/2)[0]) << "," << unsigned(graphics.Pixel(graphics.width/2,graphics.height/2)[1]) << "," << unsigned(graphics.Pixel(graphics.width/2,graphics.height/2)[2]);return count?sum/count:-1;
}
TEST(ModMeshVisualRuntime, UploadSelectionRefreshFallbackAndRetirementUseRealGpuMeshes) {
    dc::test::GfxFixture graphics(64,48,1.0f,dc::test::GfxOptions{.offscreen=true});Registered native;
    const auto original=native.source.mesh;const double before=RedCenter(graphics,native.source);std::string error;
    ASSERT_TRUE(mods::AttachReplacementVisual(native.block.data(),&native.model,native.points,native.indices,native.Asset(),error))<<error;
    EXPECT_EQ(native.source.mesh,original);EXPECT_NE(mods::ResolveReplacementVisual(native.source).mesh,original);
    EXPECT_GT(RedCenter(graphics,native.source),before+4);
    // An invalid new definition must not disturb the committed replacement.
    auto invalid=native.Asset();invalid.triangles[0].strip=7;
    const auto committed=mods::ResolveReplacementVisual(native.source).mesh;
    EXPECT_FALSE(mods::AttachReplacementVisual(native.block.data(),&native.model,native.points,native.indices,invalid,error));
    EXPECT_EQ(mods::ResolveReplacementVisual(native.source).mesh,committed);
    for(auto &v:native.source.vertices)v.position[0]-=0.25f;
    mods::RefreshReplacementVisual(native.source);EXPECT_NEAR(RedCenter(graphics,native.source),before,1);
    native.source.vertices[0].position[0]=std::numeric_limits<float>::infinity();mods::RefreshReplacementVisual(native.source);
    EXPECT_EQ(&mods::ResolveReplacementVisual(native.source),&native.source);EXPECT_FALSE(mods::ReplacementVisualError(native.source).empty());
    mods::DetachReplacementVisual(&native.source);EXPECT_EQ(mods::MeshSkinBindingCount(),0u);EXPECT_EQ(&mods::ResolveReplacementVisual(native.source),&native.source);
}
TEST(ModMeshVisualRuntime, CumulativeTriangleBudgetRejectsUploadWithoutEvictingLiveMeshes) {
    dc::test::GfxFixture graphics(64,48,1.0f,dc::test::GfxOptions{.offscreen=true});
    std::vector<std::unique_ptr<Registered>> models;std::string error;
    for(unsigned i=0;i<6;++i) {
        models.push_back(std::make_unique<Registered>());auto &native=*models.back();auto asset=native.Asset();asset.triangles.resize(200000,asset.triangles[0]);
        const bool attached=mods::AttachReplacementVisual(native.block.data(),&native.model,native.points,native.indices,asset,error);
        if(i<5){ASSERT_TRUE(attached)<<error;EXPECT_NE(&mods::ResolveReplacementVisual(native.source),&native.source);}
        else {EXPECT_FALSE(attached);EXPECT_NE(error.find("budget"),std::string::npos);EXPECT_EQ(&mods::ResolveReplacementVisual(native.source),&native.source);}
    }
    EXPECT_EQ(mods::MeshSkinBindingCount(),5u);
    for(unsigned i=0;i<5;++i)EXPECT_NE(&mods::ResolveReplacementVisual(models[i]->source),&models[i]->source);
}
TEST(ModMeshVisualRuntime, ExplicitCorrespondenceCombinesSkinAndSubsequentMorphResidual) {
    dc::test::GfxFixture graphics(64,48,1.0f,dc::test::GfxOptions{.offscreen=true});Registered native;std::string error;
    ASSERT_TRUE(mods::AttachReplacementVisual(native.block.data(),&native.model,native.points,native.indices,native.Asset(0.1f),error))<<error;
    mods::BeginNativeMeshSkin(&native.model,&native.rest,3);Matrix base,bone,inverse;bone.v[3][0]=0.2f;
    for(unsigned i=0;i<3;++i) {
        float output[3]{native.points[i][0]+0.2f,native.points[i][1],native.points[i][2]};
        mods::ApplyNativeMeshBone(i,base.v,bone.v,inverse.v,1,output);
        native.source.vertices[i].position[0]=output[0]+0.05f;
    }
    mods::RefreshReplacementVisual(native.source);const auto &visual=mods::ResolveReplacementVisual(native.source);
    ASSERT_NE(&visual,&native.source);for(unsigned i=0;i<3;++i)EXPECT_NEAR(visual.vertices[i].position[0],native.points[i][0]+0.35f,0.00001f);
    mods::RefreshReplacementVisual(native.source);for(unsigned i=0;i<3;++i)EXPECT_NEAR(visual.vertices[i].position[0],native.points[i][0]+0.35f,0.00001f);
}
}
