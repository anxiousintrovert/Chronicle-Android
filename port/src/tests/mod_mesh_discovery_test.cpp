#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include "mods/mesh_discovery.hpp"
#include "mods/mesh_asset.hpp"
#include "draw3d.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
namespace {
namespace fs=std::filesystem;
using Json=nlohmann::json;
class ModMeshDiscovery:public testing::Test {
protected:
    fs::path root=fs::temp_directory_path()/("chronicle-mesh-discovery-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    mods::MeshDiscovery discovery;std::string error;
    void SetUp() override {fs::create_directories(root);}
    void TearDown() override {std::error_code ignored;fs::remove_all(root,ignored);}
    Draw3DVisual Visual() {
        Draw3DVisual visual;
        visual.vertices={{{0,0,0},{0,0,0},{0,0},{128,128,128,128}},{{1,0,0},{0,0,2},{1,0},{128,128,128,128}},{{0,1,0},{0,0,1},{0,1},{128,128,128,128}}};
        visual.indices={0,1,2,0,0,1};visual.strips.resize(1);visual.strips[0].index_count=6;std::strcpy(visual.strips[0].texture_name,"HERO");return visual;
    }
    std::vector<fs::path> Files(const fs::path &directory) {std::vector<fs::path> result;for(const auto &entry:fs::directory_iterator(directory))result.push_back(entry.path());return result;}
};
TEST_F(ModMeshDiscovery, DormantPathDoesNotInspectVisualOrCreateFiles) {
    Draw3DVisual empty;discovery.Observe(empty,0);EXPECT_EQ(discovery.Count(),0u);EXPECT_TRUE(discovery.Errors().empty());EXPECT_TRUE(fs::is_empty(root));
}
TEST_F(ModMeshDiscovery, RejectsExistingDirectoriesAndPs2PathsBeforeAnyWrites) {
    EXPECT_FALSE(discovery.Start(root,error));EXPECT_TRUE(fs::is_empty(root));
    EXPECT_FALSE(discovery.Start(root/"PS2"/"exports",error));EXPECT_NE(error.find("PS2"),std::string::npos);EXPECT_FALSE(fs::exists(root/"PS2"));
    EXPECT_FALSE(discovery.Start(root/"missing"/"exports",error));EXPECT_FALSE(fs::exists(root/"missing"));
}
TEST_F(ModMeshDiscovery, SnapshotRoundTripsAndDeduplicatesWithoutMutatingNativeGeometry) {
    ASSERT_TRUE(discovery.Start(root/"export",error))<<error;auto visual=Visual();const auto before=visual;
    discovery.Observe(visual,77);discovery.Observe(visual,77);ASSERT_EQ(discovery.Count(),1u);EXPECT_TRUE(discovery.Errors().empty());
    const auto files=Files(root/"export");ASSERT_EQ(files.size(),1u);std::ifstream input(files[0]);Json snapshot;input>>snapshot;
    EXPECT_EQ(snapshot["native_vertices"],77);EXPECT_EQ(snapshot["textures"],Json::array({"HERO"}));
    EXPECT_EQ(snapshot["fallback_normals"],1);EXPECT_EQ(snapshot["dropped_degenerate_triangles"],1);
    const auto mesh=mods::DecodeMeshAsset(snapshot["mesh"].dump());EXPECT_EQ(mesh.vertices.size(),3u);EXPECT_EQ(mesh.triangles.size(),1u);
    EXPECT_EQ(visual.indices,before.indices);EXPECT_EQ(std::memcmp(visual.vertices.data(),before.vertices.data(),visual.vertices.size()*sizeof(gfx::Vertex3D)),0);
    discovery.Stop();visual.vertices[0].position[0]=-1;discovery.Observe(visual,77);EXPECT_EQ(discovery.Count(),1u);
}
TEST_F(ModMeshDiscovery, InvalidGeometryIsReportedOnceAndDoesNotPublishPartialSnapshot) {
    ASSERT_TRUE(discovery.Start(root/"export",error));auto visual=Visual();visual.indices[0]=99;
    discovery.Observe(visual,3);discovery.Observe(visual,3);EXPECT_EQ(discovery.Errors().size(),1u);EXPECT_TRUE(fs::is_empty(root/"export"));
    visual=Visual();discovery.Observe(visual,3);EXPECT_EQ(discovery.Count(),1u);
}
TEST_F(ModMeshDiscovery, OutputBudgetStopsCaptureAndNeverOverwritesExistingSnapshots) {
    ASSERT_TRUE(discovery.Start(root/"export",error));auto visual=Visual();
    for(unsigned i=0;i<257;++i) {for(auto &v:visual.vertices)v.position[0]+=1;discovery.Observe(visual,3);}
    EXPECT_EQ(discovery.Count(),256u);EXPECT_FALSE(discovery.Active());EXPECT_EQ(discovery.Errors().size(),1u);EXPECT_EQ(Files(root/"export").size(),256u);
    EXPECT_FALSE(discovery.Start(root/"export",error));EXPECT_EQ(Files(root/"export").size(),256u);
}
}
