#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include "mods/texture_packages.hpp"
#include "mods/session.hpp"
#include "tex_fixture.hpp"
namespace {
using Json=nlohmann::json;namespace fs=std::filesystem;
const unsigned char kPng[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x7f, 0xa8, 0x7d, 0x63, 0x00, 0x00, 0x00, 0x15, 0x49,
    0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x84, 0xa1, 0xd4, 0x7f, 0x06, 0x34, 0x01, 0x06,
    0x00, 0x06, 0x2a, 0x0e, 0xf2, 0x17, 0xef, 0xa6, 0x26, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42,
    0x60, 0x82};


class ModTexturePackage:public testing::Test {
protected:
    fs::path root=fs::temp_directory_path()/("chronicle-textures-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    mods::Manager manager;
    PortDecodedTexture Original() {PortDecodedTexture t;t.width=2;t.height=1;t.levels={{255,0,0,255,255,0,0,255}};return t;}
    void Add(const std::string &id) {
        fs::create_directories(root/id);std::ofstream(root/id/"image.png",std::ios::binary).write(reinterpret_cast<const char *>(kPng),sizeof(kPng));
        std::ofstream(root/id/"mod.json")<<Json{{"schema_version",1},{"id",id},{"name",id},{"version","1"},{"game","chronicle"},{"files",Json::object()},
          {"requires_capabilities",{"assets.textures.v1"}},{"textures",{{{"name","atlas"},{"original_sha256",mods::TexturePixelIdentity(Original())},{"source","image.png"}}}}}.dump();
    }
    void TearDown()override {mods::Shared().Start({}, {}, root);std::error_code error;fs::remove_all(root,error);}
};
TEST_F(ModTexturePackage, DisabledConflictAndOriginalHashMismatchRetainOriginal) {
    Add("test.one");manager.Scan({root},{});EXPECT_TRUE(manager.Textures().empty());
    manager.Scan({root},{},{{"test.one",true}});ASSERT_EQ(manager.Textures().size(),1u);
    auto decoded=Original();const auto before=decoded.levels;std::string error;
    EXPECT_FALSE(mods::ApplyTextureReplacement(manager,"atlas",std::string(64,'0'),decoded,error));EXPECT_EQ(decoded.levels,before);
    ASSERT_TRUE(mods::ApplyTextureReplacement(manager,"ATLAS",mods::TexturePixelIdentity(decoded),decoded,error))<<error;
    EXPECT_EQ(decoded.width,4u);EXPECT_EQ(decoded.height,2u);EXPECT_EQ(decoded.logical_width,2u);EXPECT_EQ(decoded.logical_height,1u);EXPECT_EQ(decoded.levels[0].size(),32u);
    Add("test.two");manager.Scan({root},{},{{"test.one",true},{"test.two",true}});EXPECT_TRUE(manager.Textures().empty());
    for(const auto &p:manager.Packages())EXPECT_FALSE(p.problems.empty());
}
TEST_F(ModTexturePackage, ChangedSourceFallsBackAndRescanValidatesMalformedImage) {
    Add("test.one");manager.Scan({root},{},{{"test.one",true}});ASSERT_EQ(manager.Textures().size(),1u);
    std::ofstream(root/"test.one/image.png",std::ios::binary)<<"invalid";
    auto decoded=Original();const auto before=decoded.levels;std::string error;
    EXPECT_FALSE(mods::ApplyTextureReplacement(manager,"atlas",mods::TexturePixelIdentity(decoded),decoded,error));EXPECT_FALSE(error.empty());EXPECT_EQ(decoded.levels,before);
    manager.Scan({root},{},{{"test.one",true}});EXPECT_TRUE(manager.Textures().empty());ASSERT_EQ(manager.Packages().size(),1u);EXPECT_FALSE(manager.Packages()[0].problems.empty());
}
TEST_F(ModTexturePackage, NativeTextureEntryUploadsFullResolutionAndRestartDisablesIt) {
    Add("test.one");std::ofstream(root/"mods-profile.json")<<Json{{"schema_version",1},{"enabled",{{"test.one",true}}}}.dump();
    mods::Shared().Start({root},{},root);ASSERT_EQ(mods::Shared().Active().Textures().size(),1u);
    texfix::TexEnv env;auto img=texfix::Img({{"atlas",texfix::Tim2({TIM2_RGB32,2,1,{texfix::Rgba32({texfix::Gs(255,0,0),texfix::Gs(255,0,0)})},{},0})}});
    TexManager.BeginEnterTextureBlock(0);TexManager.EnterIMGFile(img.data(),0,0,0);TexManager.EndEnterTextureBlock(0);
    char name[]="atlas";auto ref=PortTextureFromCTexture(TexManager.GetTexture(name,-1));ASSERT_TRUE(ref.valid);
    auto info=gfx::GetTextureInfo(ref.binding.texture);ASSERT_TRUE(info);EXPECT_EQ(info->pixel_width,4u);EXPECT_EQ(info->pixel_height,2u);EXPECT_EQ(info->width,2u);
    env.gfx.Frame({0,0,0,128},[&]{env.Draw(ref,0,0,320,160,0,0,2,1);});EXPECT_TRUE(env.gfx.PixelNear(80,40,255,0,0));EXPECT_TRUE(env.gfx.PixelNear(240,40,0,0,255));
    ASSERT_TRUE(mods::Shared().SetEnabled("test.one",false));EXPECT_EQ(mods::Shared().Active().Textures().size(),1u);
    mods::Shared().Start({root},{},root);EXPECT_TRUE(mods::Shared().Active().Textures().empty());
    TexManager.Initialize(16352);TexManager.SetBuffer(texfix::staging,static_cast<int>(std::size(texfix::staging)));
    TexManager.BeginEnterTextureBlock(0);TexManager.EnterIMGFile(img.data(),0,0,0);TexManager.EndEnterTextureBlock(0);
    ref=PortTextureFromCTexture(TexManager.GetTexture(name,-1));ASSERT_TRUE(ref.valid);info=gfx::GetTextureInfo(ref.binding.texture);ASSERT_TRUE(info);EXPECT_EQ(info->pixel_width,2u);
}
}
