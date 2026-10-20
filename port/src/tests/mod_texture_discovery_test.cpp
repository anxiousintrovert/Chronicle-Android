#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include "mods/texture_discovery.hpp"
#include "mods/texture_packages.hpp"
#include <chrono>
#include <fstream>
namespace {
namespace fs=std::filesystem;using Json=nlohmann::json;
class ModTextureDiscovery:public testing::Test {
protected:
    fs::path root=fs::temp_directory_path()/("chronicle-texture-discovery-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    mods::TextureDiscovery discovery;std::string error;
    void SetUp()override{fs::create_directory(root);}
    void TearDown()override{std::error_code ignored;fs::remove_all(root,ignored);}
    PortDecodedTexture Image(){PortDecodedTexture t;t.width=2;t.height=1;t.format=gfx::TextureFormat::Index8;t.levels={{0,1}};t.palette[0]=0xff0000ff;t.palette[1]=0x0000ff00;return t;}
};
TEST_F(ModTextureDiscovery, DormantAndRejectedPathsNeverWrite) {
    discovery.Observe("invalid",{});EXPECT_TRUE(fs::is_empty(root));EXPECT_TRUE(discovery.Errors().empty());
    EXPECT_FALSE(discovery.Start(root,error));EXPECT_FALSE(discovery.Start(root/"PS2"/"out",error));EXPECT_FALSE(fs::exists(root/"PS2"));
}
TEST_F(ModTextureDiscovery, CapturesOriginalIndexedAlphaAndDeduplicatesByNameAndContent) {
    ASSERT_TRUE(discovery.Start(root/"out",error));auto texture=Image();const auto before=texture.levels;
    discovery.Observe("ATLAS",texture);discovery.Observe("atlas",texture);ASSERT_EQ(discovery.Count(),1u);
    const auto file=fs::directory_iterator(root/"out")->path();std::ifstream stream(file,std::ios::binary);std::string header;std::getline(stream,header);
    const auto info=Json::parse(header);EXPECT_EQ(info["original_sha256"],mods::TexturePixelIdentity(texture));EXPECT_EQ(info["name"],"atlas");
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)),{});EXPECT_EQ(bytes,(std::vector<unsigned char>{255,0,0,255,0,255,0,0}));
    EXPECT_EQ(texture.levels,before);discovery.Observe("other",texture);EXPECT_EQ(discovery.Count(),2u);
    discovery.Stop();discovery.Observe("third",texture);EXPECT_EQ(discovery.Count(),2u);EXPECT_TRUE(discovery.Errors().empty());
}
TEST_F(ModTextureDiscovery, InvalidInputDoesNotPublishAndErrorsAreDeduplicated) {
    ASSERT_TRUE(discovery.Start(root/"out",error));discovery.Observe("atlas",{});discovery.Observe("atlas",{});
    EXPECT_EQ(discovery.Errors().size(),1u);EXPECT_TRUE(fs::is_empty(root/"out"));
    discovery.Observe("../bad",Image());EXPECT_TRUE(fs::is_empty(root/"out"));
}
}
