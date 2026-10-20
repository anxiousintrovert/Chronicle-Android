#include <gtest/gtest.h>
#include "mods/mesh_catalog.hpp"
#include <chrono>
#include <fstream>
namespace {
namespace fs=std::filesystem;
constexpr auto geometry=R"({"schema_version":1,"vertices":[[0,0,0,0,0,1,0,0],[1,0,0,0,0,1,1,0],[0,1,0,0,0,1,0,1]],"triangles":[[0,0,1,2]]})";
class ModMeshCatalog:public testing::Test {
protected:
    fs::path root=fs::temp_directory_path()/("chronicle-mesh-catalog-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    mods::MeshCatalog catalog;std::string error;
    void SetUp() override {fs::create_directories(root);}
    void TearDown() override {std::error_code ignored;fs::remove_all(root,ignored);}
    void Write(const fs::path &path,const std::string &text) {fs::create_directories(path.parent_path());std::ofstream stream(path,std::ios::binary);stream<<text;ASSERT_TRUE(stream.good());}
    mods::Package Package(std::string id,std::string texture="hero",unsigned count=0) {
        mods::Package package;package.id=id;package.root=root/id;package.enabled=true;
        Write(package.root/"mesh.json",geometry);package.meshes.push_back({texture,count,package.root/"mesh.json"});return package;
    }
    mods::MeshCatalog::Match Select(std::initializer_list<std::string_view> textures,unsigned count=3) {return catalog.Select({textures.begin(),textures.size()},count);}
};
TEST_F(ModMeshCatalog, InactivePackagesAndEmptyCatalogDoNoFilesystemWork) {
    mods::Package package;package.id="off";package.root=root/"missing";package.meshes.push_back({"hero",0,root/"absent.json"});
    EXPECT_TRUE(catalog.LoadPackage(package,error));EXPECT_TRUE(error.empty());EXPECT_EQ(catalog.Size(),0u);EXPECT_FALSE(Select({"hero"}).Unique());
    package.enabled=true;package.problems.push_back("blocked");EXPECT_TRUE(catalog.LoadPackage(package,error));EXPECT_TRUE(error.empty());
}
TEST_F(ModMeshCatalog, ImmutableSnapshotMatchesCaseCountAndSurvivesSourceRemoval) {
    auto package=Package("one","HeRo",3);ASSERT_TRUE(catalog.LoadPackage(package,error))<<error;
    auto match=Select({"hero","HERO"});ASSERT_TRUE(match.Unique());EXPECT_EQ(match.first->owner,"one");EXPECT_EQ(match.first->asset.vertices.size(),3u);
    EXPECT_FALSE(Select({"hero"},4).Unique());EXPECT_FALSE(Select({"hero"},0).Unique());EXPECT_FALSE(Select({"other"}).Unique());
    fs::remove(package.meshes[0].source);EXPECT_TRUE(Select({"HERO"}).Unique());
    catalog.Clear();EXPECT_EQ(catalog.Size(),0u);EXPECT_EQ(match.first->asset.triangles.size(),1u);
}
TEST_F(ModMeshCatalog, RuntimeMultiTextureAmbiguityNeverSelectsAnArbitraryWinner) {
    auto first=Package("one","body"),second=Package("two","head");
    ASSERT_TRUE(catalog.LoadPackage(first,error))<<error;ASSERT_TRUE(catalog.LoadPackage(second,error))<<error;
    auto match=Select({"head","body"});EXPECT_FALSE(match.Unique());ASSERT_TRUE(match.first);ASSERT_TRUE(match.conflict);EXPECT_NE(match.first->owner,match.conflict->owner);
    catalog.Retain(std::array<std::string,1>{"two"});EXPECT_EQ(catalog.Size(),1u);EXPECT_TRUE(Select({"head","body"}).Unique());
    second.meshes.push_back({"body",0,second.meshes[0].source});ASSERT_TRUE(catalog.LoadPackage(second,error))<<error;
    EXPECT_FALSE(Select({"body","head"}).Unique());EXPECT_TRUE(Select({"head","head"}).Unique());
}
TEST_F(ModMeshCatalog, FailedOwnerReplacementIsAtomicAndSuccessfulReloadReplacesItsTargets) {
    auto package=Package("one");ASSERT_TRUE(catalog.LoadPackage(package,error))<<error;auto original=Select({"hero"}).first;
    package.meshes[0].texture="new";package.meshes.push_back({"bad",0,package.root/"bad.json"});Write(package.root/"bad.json","{}");
    EXPECT_FALSE(catalog.LoadPackage(package,error));EXPECT_EQ(Select({"hero"}).first,original);EXPECT_FALSE(Select({"new"}).Unique());
    package.meshes.pop_back();ASSERT_TRUE(catalog.LoadPackage(package,error))<<error;EXPECT_FALSE(Select({"hero"}).Unique());EXPECT_TRUE(Select({"new"}).Unique());
    EXPECT_EQ(original->asset.vertices.size(),3u);package.meshes.clear();ASSERT_TRUE(catalog.LoadPackage(package,error));EXPECT_EQ(catalog.Size(),0u);
}
TEST_F(ModMeshCatalog, RejectsEscapingMissingAndOverlappingTargetsWithoutEviction) {
    auto first=Package("one");ASSERT_TRUE(catalog.LoadPackage(first,error));
    auto second=Package("two","HERO",3);EXPECT_FALSE(catalog.LoadPackage(second,error));EXPECT_EQ(catalog.Size(),1u);EXPECT_NE(error.find("overlapping"),std::string::npos);
    second.meshes[0].texture="other";second.meshes[0].source=first.meshes[0].source;
    EXPECT_FALSE(catalog.LoadPackage(second,error));EXPECT_NE(error.find("escapes"),std::string::npos);
    second.meshes[0].source=second.root/"missing";EXPECT_FALSE(catalog.LoadPackage(second,error));EXPECT_TRUE(Select({"hero"}).Unique());
    second.meshes[0].source=second.root/"mesh.json";second.meshes[0].texture="bad/name";
    EXPECT_FALSE(catalog.LoadPackage(second,error));EXPECT_NE(error.find("texture"),std::string::npos);
}
TEST_F(ModMeshCatalog, ExactCountsCoexistAndGeometryBudgetRejectsWithoutEviction) {
    auto first=Package("one","hero",3),second=Package("two","HERO",4);
    ASSERT_TRUE(catalog.LoadPackage(first,error));ASSERT_TRUE(catalog.LoadPackage(second,error));
    EXPECT_EQ(Select({"hero"},3).first->owner,"one");EXPECT_EQ(Select({"hero"},4).first->owner,"two");
    catalog.Clear();std::string large=geometry;large.erase(large.find("[[0,0,1,2]]"));large+='[';
    for(unsigned i=0;i<200000;++i){if(i)large+=',';large+="[0,0,1,2]";}large+="]}";
    for(unsigned i=0;i<6;++i) {
        auto package=Package("owner"+std::to_string(i),"texture"+std::to_string(i));Write(package.meshes[0].source,large);
        const bool loaded=catalog.LoadPackage(package,error);
        if(i<5)ASSERT_TRUE(loaded)<<error;
        else {EXPECT_FALSE(loaded);EXPECT_NE(error.find("geometry budget"),std::string::npos);}
    }
    EXPECT_EQ(catalog.Size(),5u);EXPECT_TRUE(Select({"texture0"}).Unique());EXPECT_FALSE(Select({"texture5"}).Unique());
}
}
