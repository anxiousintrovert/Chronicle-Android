#include <gtest/gtest.h>
#include "mods/mesh_visual.hpp"
#include <cstring>
#include <limits>
namespace {
Draw3DVisual Source() {
    Draw3DVisual source;source.vertex_colour=true;source.vertices.resize(3);source.strips.resize(2);
    source.vertices[1].position[0]=1;source.vertices[2].position[1]=1;
    for(unsigned i=0;i<3;++i)for(unsigned c=0;c<4;++c)source.vertices[i].color[c]=10*i+c;
    source.strips[0].texture=7;source.strips[1].texture=9;std::strcpy(source.strips[1].texture_name,"native");
    source.strips[1].tex0=123;source.strips[1].diffuse[2]=0.25f;source.strips[1].ground_world_uv=true;
    return source;
}
mods::MeshAsset Asset() {
    return {{{0,0,0,0,0,1,0,0},{1,0,0,0,0,1,1,0},{0,1,0,0,0,1,0,1}},{{1,{0,1,2}},{0,{2,1,0}},{1,{2,0,1}}}};
}
TEST(ModMeshVisual, BuildsDetachedGeometryWithStableStripGroupingAndNativeMaterials) {
    const auto source=Source();const auto prepared=mods::PrepareMeshVisual(source,Asset());const auto &v=prepared.visual;
    EXPECT_EQ(v.mesh,gfx::kNullMesh);EXPECT_FALSE(v.immediate);EXPECT_TRUE(v.vertex_colour);
    EXPECT_EQ(v.indices,(std::vector<std::uint32_t>{2,1,0,0,1,2,2,0,1}));
    EXPECT_EQ(v.strips[0].first_index,0u);EXPECT_EQ(v.strips[0].index_count,3u);EXPECT_EQ(v.strips[1].first_index,3u);EXPECT_EQ(v.strips[1].index_count,6u);
    EXPECT_EQ(v.strips[1].texture,9);EXPECT_STREQ(v.strips[1].texture_name,"native");EXPECT_EQ(v.strips[1].tex0,123u);EXPECT_EQ(v.strips[1].diffuse[2],0.25f);EXPECT_TRUE(v.strips[1].ground_world_uv);
    EXPECT_EQ(v.vertices[1].uv[0],1);EXPECT_EQ(v.vertices[1].color[2],12);EXPECT_TRUE(source.indices.empty());
}
TEST(ModMeshVisual, MorphAndMaterialRefreshPreservesReplacementTopologyUvsAndNormals) {
    auto source=Source();auto prepared=mods::PrepareMeshVisual(source,Asset());source.vertices[1].position[0]=5;source.vertices[1].color[0]=200;
    source.strips[1].texture=17;source.strips[1].first_index=900;source.strips[1].index_count=900;source.strips[1].diffuse[0]=0.5f;
    ASSERT_TRUE(mods::RefreshMeshVisual(prepared,source));EXPECT_EQ(prepared.visual.vertices[1].position[0],5);EXPECT_EQ(prepared.visual.vertices[1].color[0],200);
    EXPECT_EQ(prepared.visual.vertices[1].uv[0],1);EXPECT_EQ(prepared.visual.vertices[1].normal[2],1);EXPECT_EQ(prepared.visual.strips[1].first_index,3u);EXPECT_EQ(prepared.visual.strips[1].index_count,6u);EXPECT_EQ(prepared.visual.strips[1].texture,17);
    ASSERT_TRUE(mods::RefreshMeshVisual(prepared,source));EXPECT_EQ(prepared.visual.vertices[1].position[0],5);
    const mods::MeshSkinBinding::Vertex pose[]{{9,8,7,1},{6,5,4,1},{3,2,1,1}};
    ASSERT_TRUE(mods::RefreshMeshVisual(prepared,source,pose));EXPECT_EQ(prepared.visual.vertices[1].position[0],6);
}
TEST(ModMeshVisual, RejectedRefreshDoesNotPartiallyPublishVerticesOrMaterials) {
    auto source=Source();auto prepared=mods::PrepareMeshVisual(source,Asset());const auto before=prepared.visual.vertices;
    source.vertices[0].position[0]=5;source.vertices[2].position[0]=std::numeric_limits<float>::infinity();source.strips[0].texture=99;
    EXPECT_FALSE(mods::RefreshMeshVisual(prepared,source));EXPECT_EQ(std::memcmp(before.data(),prepared.visual.vertices.data(),before.size()*sizeof(gfx::Vertex3D)),0);EXPECT_EQ(prepared.visual.strips[0].texture,7);
    source=Source();source.vertices.pop_back();EXPECT_FALSE(mods::RefreshMeshVisual(prepared,source));
}
TEST(ModMeshVisual, RejectsUnsupportedSourceAndInvalidMaterialTargetsBeforePublication) {
    auto source=Source();auto asset=Asset();asset.triangles[0].strip=2;EXPECT_ANY_THROW(mods::PrepareMeshVisual(source,asset));
    asset=Asset();asset.triangles[0].vertices[0]=3;EXPECT_ANY_THROW(mods::PrepareMeshVisual(source,asset));
    asset=Asset();asset.vertices[2]=asset.vertices[1];EXPECT_ANY_THROW(mods::PrepareMeshVisual(source,asset));
    asset=Asset();asset.vertices[0][6]=1000001;EXPECT_ANY_THROW(mods::PrepareMeshVisual(source,asset));
    asset=Asset();source.immediate=true;EXPECT_ANY_THROW(mods::PrepareMeshVisual(source,asset));
    source=Source();source.strips[0].diffuse[0]=NAN;EXPECT_ANY_THROW(mods::PrepareMeshVisual(source,asset));
}
}
