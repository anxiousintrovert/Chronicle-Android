#include "tests/gfx_fixture.hpp"
#include <gtest/gtest.h>
#include "mods/runtime.hpp"
#include "mods/mesh_packages.hpp"
#include "mods/mesh_discovery.hpp"
#include "mods/mesh_visual_runtime.hpp"
#include "mods/mesh_skin_runtime.hpp"
#include <chrono>
#include <fstream>
#include <cstring>
#include <limits>
#include "tex_fixture.hpp"
#include "mdt.hpp"
#include "visual.hpp"
namespace {
namespace fs=std::filesystem;
struct alignas(16) Model {
    MDT_HEADER header{};
    alignas(16) float positions[3][4]{{-.5f,-.5f,.5f,1},{.5f,-.5f,.5f,1},{0,.5f,.5f,1}};
    alignas(16) float normals[3][4]{{0,0,1,0},{0,0,1,0},{0,0,1,0}};
    alignas(16) float uv[3][4]{};
    MDT_MATERIAL materials[2]{};
    unsigned mesh[28]{};
    Model(unsigned strips=1) {
        header.size=sizeof(*this);header.vertex_num=3;header.vertex_ofs=offsetof(Model,positions);
        header.normal[1]=offsetof(Model,normals);header.uv[1]=offsetof(Model,uv);
        header.info_ofs=offsetof(Model,materials);header.mesh_ofs=offsetof(Model,mesh);
        std::strcpy(materials[0].texture,"hero");std::strcpy(materials[1].texture,"head");
        mesh[2]=strips;
        for(unsigned s=0;s<strips;++s) {
            auto *index=mesh+4+s*12;index[0]=3;index[1]=3;index[2]=s;
            for(unsigned i=0;i<3;++i)for(unsigned field=0;field<3;++field)index[3+i*3+field]=i;
        }
    }
};
class ModMeshPackages:public testing::Test {
protected:
    texfix::TexEnv env;
    texfix::Bytes img;
    std::array<unsigned,16> block{};
    fs::path root=fs::temp_directory_path()/("chronicle-mesh-packages-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    void SetUp() override {
        mods::Scripts().Start(mods::Manager{});fs::create_directories(root);
        texfix::Bytes white(2*2*4,128);
        img=texfix::Img({{"hero",texfix::Tim2({TIM2_RGB32,2,2,{white},{},0})},{"head",texfix::Tim2({TIM2_RGB32,2,2,{white},{},0})}});
        TexManager.BeginEnterTextureBlock(1);TexManager.EnterIMGFile(img.data(),1,0,0);TexManager.EndEnterTextureBlock(1);
    }
    void TearDown() override {
        mods::MeshDiscoveryRecorder().Stop();mods::Scripts().Start(mods::Manager{});Draw3DRetireVisuals(block.data(),sizeof(block));
        std::error_code ignored;fs::remove_all(root,ignored);
    }
    mods::Package Package(const std::string &id,const std::string &texture,unsigned strip=0) {
        mods::Package package;package.id=id;package.enabled=true;package.root=root/id;fs::create_directories(package.root);
        const auto path=package.root/"mesh.json";
        std::ofstream output(path);output<<R"({"schema_version":1,"vertices":[[-0.25,-0.5,0.5,0,0,1,0,0],[0.75,-0.5,0.5,0,0,1,1,0],[0.25,0.5,0.5,0,0,1,0,1]],"triangles":[[)"<<strip<<",0,1,2]]}";output.close();
        package.meshes.push_back({texture,3,path});return package;
    }
    void Load(const mods::Package &package) {std::string error;ASSERT_TRUE(mods::Scripts().Meshes().LoadPackage(package,error))<<error;}
    Draw3DVisual &Build(CVisualVu1 &visual,Model &model) {
        visual.CreateVUdataFromMDT(block.data(),reinterpret_cast<unsigned *>(&model),0,0);
        return *const_cast<Draw3DVisual *>(Draw3DFindVisual(block.data()));
    }
};
TEST_F(ModMeshPackages, NativeConstructionIsDormantThenAttachesCachedGeometryAndRetiresOwner) {
    Model model;const auto original=model;CVisualVu1 native;native.Initialize();auto &before=Build(native,model);
    EXPECT_EQ(&mods::ResolveReplacementVisual(before),&before);EXPECT_EQ(mods::MeshSkinBindingCount(),0u);EXPECT_EQ(std::memcmp(&model,&original,sizeof(model)),0);
    auto package=Package("test.mesh","HERO");Load(package);auto &source=Build(native,model);
    const auto &replacement=mods::ResolveReplacementVisual(source);ASSERT_NE(&replacement,&source);
    EXPECT_NE(replacement.mesh,source.mesh);EXPECT_EQ(mods::MeshSkinBindingCount(),1u);EXPECT_FLOAT_EQ(replacement.vertices[0].position[0],-.25f);
    EXPECT_EQ(std::memcmp(&model,&original,sizeof(model)),0);fs::remove(package.meshes[0].source);
    for(auto &position:model.positions)position[0]+=.125f;
    native.CreateVUdataFromMDTRemake(block.data(),reinterpret_cast<unsigned *>(&model),1);
    EXPECT_FLOAT_EQ(mods::ResolveReplacementVisual(source).vertices[0].position[0],-.125f);
    EXPECT_TRUE(mods::Scripts().Diagnostics().empty());
    // The fixture's manually loaded owner has no runtime module. Tick must retire it.
    mods::Scripts().Tick(mods::World{});
    EXPECT_FALSE(mods::PackageMeshesActive());EXPECT_EQ(&mods::ResolveReplacementVisual(source),&source);EXPECT_EQ(mods::MeshSkinBindingCount(),0u);
}
TEST_F(ModMeshPackages, AmbiguousNativeMultiTextureModelKeepsOriginalAndReportsBothOwners) {
    Load(Package("body.mod","hero"));Load(Package("head.mod","head"));Model model(2);CVisualVu1 native;native.Initialize();auto &source=Build(native,model);
    EXPECT_EQ(&mods::ResolveReplacementVisual(source),&source);EXPECT_EQ(mods::MeshSkinBindingCount(),0u);
    const auto diagnostics=mods::Scripts().Diagnostics();ASSERT_EQ(diagnostics.size(),2u);
    EXPECT_TRUE(diagnostics[0].starts_with("body.mod:"));EXPECT_TRUE(diagnostics[1].starts_with("head.mod:"));
    EXPECT_NE(diagnostics[0].find("ambiguous native model"),std::string::npos);
    Build(native,model);EXPECT_EQ(mods::Scripts().Diagnostics().size(),2u);
}
TEST_F(ModMeshPackages, UnsupportedStripAndFailedRefreshAreVisibleWithoutLosingOriginalMesh) {
    Load(Package("test.mesh","hero",7));Model model;CVisualVu1 native;native.Initialize();auto &bad=Build(native,model);
    EXPECT_EQ(&mods::ResolveReplacementVisual(bad),&bad);ASSERT_FALSE(mods::Scripts().Diagnostics().empty());
    mods::Scripts().Meshes().Clear();Load(Package("test.mesh","hero"));auto &source=Build(native,model);
    ASSERT_NE(&mods::ResolveReplacementVisual(source),&source);
    for(auto &position:model.positions)position[0]=std::numeric_limits<float>::infinity();
    native.CreateVUdataFromMDTRemake(block.data(),reinterpret_cast<unsigned *>(&model),1);
    EXPECT_EQ(&mods::ResolveReplacementVisual(source),&source);
    const auto diagnostics=mods::Scripts().Diagnostics();ASSERT_EQ(diagnostics.size(),1u);EXPECT_TRUE(diagnostics[0].starts_with("test.mesh: mesh refresh:"));
    mods::Scripts().Meshes().Clear();EXPECT_EQ(mods::MeshSkinBindingCount(),0u);
}
TEST_F(ModMeshPackages, CallbackFailureRetiresOwnedGeometryBeforeTheNextDraw) {
    auto package=Package("test.mesh","hero");
    {
        std::ofstream manifest(package.root/"mod.json");
        manifest<<R"({"schema_version":1,"api_version":1,"id":"test.mesh","name":"Mesh owner","version":"1.0.0","game":"chronicle","files":{},"script":"main.lua"})";
        std::ofstream script(package.root/"main.lua");script<<"return {on_tick=function() error('intentional owner failure') end}";
    }
    mods::Manager manager;manager.Scan({root},{},{{"test.mesh",true}});ASSERT_EQ(manager.LoadOrder().size(),1u);
    mods::Scripts().Start(manager);ASSERT_EQ(mods::Scripts().ActiveOwners().size(),1u);
    Load(package);Model model;CVisualVu1 native;native.Initialize();auto &source=Build(native,model);
    ASSERT_NE(&mods::ResolveReplacementVisual(source),&source);
    mods::World world;world.playing=true;world.mode=1;world.map=0;mods::Scripts().Tick(world);
    EXPECT_TRUE(mods::Scripts().ActiveOwners().empty());EXPECT_FALSE(mods::PackageMeshesActive());
    EXPECT_EQ(&mods::ResolveReplacementVisual(source),&source);EXPECT_EQ(mods::MeshSkinBindingCount(),0u);
    ASSERT_FALSE(mods::Scripts().Diagnostics().empty());
}
TEST_F(ModMeshPackages, OptInDiscoveryCapturesNativeConstructionBeforeIndicesAreReleased) {
    std::string error;ASSERT_TRUE(mods::MeshDiscoveryRecorder().Start(root/"export",error))<<error;
    Model model;CVisualVu1 native;native.Initialize();auto &source=Build(native,model);
    EXPECT_TRUE(source.indices.empty());EXPECT_EQ(mods::MeshDiscoveryRecorder().Count(),1u);
    EXPECT_EQ(&mods::ResolveReplacementVisual(source),&source);EXPECT_EQ(mods::MeshSkinBindingCount(),0u);
    Build(native,model);EXPECT_EQ(mods::MeshDiscoveryRecorder().Count(),1u);
    EXPECT_TRUE(mods::MeshDiscoveryRecorder().Errors().empty());
}
TEST_F(ModMeshPackages, UnmatchedNativeModelDoesNotCreateBindingsOrDiagnostics) {
    Load(Package("test.mesh","unseen"));Model model;CVisualVu1 native;native.Initialize();auto &source=Build(native,model);
    EXPECT_EQ(&mods::ResolveReplacementVisual(source),&source);EXPECT_EQ(mods::MeshSkinBindingCount(),0u);EXPECT_TRUE(mods::Scripts().Diagnostics().empty());
}
}
