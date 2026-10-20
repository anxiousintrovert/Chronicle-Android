#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include "gfx_fixture.hpp"
#include "gameloop.hpp"
#include "mods/party.hpp"
#include "mods/native.hpp"
#include "mods/runtime.hpp"
#include "mods/session.hpp"
#include "mods/texture_discovery.hpp"
#include "platform/clock.hpp"
#include "platform/config.hpp"
#include "platform/input_script.hpp"
#include "platform/paths.hpp"
#include "texture.hpp"
#include "texture_port.hpp"
#include <fstream>
namespace {
namespace fs=std::filesystem;using Json=nlohmann::json;
fs::path texture_output;unsigned expected_pixels=256;std::uint64_t ready_frame=0;bool completed=false;
void TexturePump() {
    WindowPollEvents();InputPoll();InputScriptApply(GameFrameCount());
    if(::testing::Test::HasFatalFailure()){GameRequestStop();return;}
    if(!ready_frame) {
        if(!mods::ReadPartySwitchState().value("ready",false))return;
        ready_frame=GameFrameCount();InputScript release;std::string error;const auto turn="0\n"+std::to_string(ready_frame+40)+" down\n"+std::to_string(ready_frame+45)+"\n";ASSERT_TRUE(InputScriptParse(turn,release,error));InputScriptInstall(std::move(release));return;
    }
    if(GameFrameCount()<ready_frame+120)return;
    char name[]="c01d03";auto *native=TexManager.GetTexture(name,-1);ASSERT_NE(native,nullptr);
    const auto ref=PortTextureFromCTexture(native);ASSERT_TRUE(ref.valid);const auto info=gfx::GetTextureInfo(ref.binding.texture);ASSERT_TRUE(info);
    EXPECT_EQ(info->width,256u);EXPECT_EQ(info->height,256u);EXPECT_EQ(info->pixel_width,expected_pixels);EXPECT_EQ(info->pixel_height,expected_pixels);
    EXPECT_EQ(native->width,256);EXPECT_EQ(native->height,256);EXPECT_FALSE(mods::Scripts().HasGameplay());
    std::string pixel_digest;
    if(expected_pixels>256) {
        EXPECT_EQ(info->format,gfx::TextureFormat::Rgba8);
        std::vector<std::uint8_t> pixels;std::uint32_t w=0,h=0;ASSERT_TRUE(gfx::ReadbackTexture(ref.binding.texture,pixels,w,h));
        ASSERT_EQ(pixels.size(),static_cast<std::size_t>(expected_pixels)*expected_pixels*4u);
        pixel_digest=mods::Sha256(std::string_view(reinterpret_cast<const char *>(pixels.data()),pixels.size()));
        if(expected_pixels==512){EXPECT_EQ(pixels[0],255);EXPECT_EQ(pixels[1],0);EXPECT_EQ(pixels[2],255);}
        else {std::ifstream input(texture_output/"expected-rgba.sha256");std::string expected;input>>expected;ASSERT_EQ(expected.size(),64u);EXPECT_EQ(pixel_digest,expected);}
    }
    std::vector<std::uint8_t> screen;std::uint32_t w=0,h=0;ASSERT_TRUE(GameScreenshot(screen,w,h));ASSERT_TRUE(gfx::WritePng(texture_output/"scene.png",screen.data(),w,h));
    std::ofstream(texture_output/"texture-result.json")<<Json{{"logical_width",info->width},{"logical_height",info->height},{"pixel_width",info->pixel_width},{"pixel_height",info->pixel_height},{"frame",GameFrameCount()},{"gameplay_mods",mods::Scripts().HasGameplay()},{"active_targets",mods::Shared().Active().Textures().size()},{"gpu_rgba_sha256",pixel_digest}}.dump(2);
    completed=true;GameRequestStop();
}
TEST(ModTextureRetail, LoadedPlayerTexturePreservesCoordinatesAndFullPixels) {
    const char *data=std::getenv("DC_DATA"),*audit=std::getenv("DC_TEXTURE_AUDIT"),*expected=std::getenv("DC_TEXTURE_EXPECT");
    if(!data||!audit||!expected)GTEST_SKIP()<<"Run with isolated DC_TEXTURE_AUDIT, DC_DATA and DC_TEXTURE_EXPECT=256, 512 or 4096";
    ASSERT_TRUE(std::string(expected)=="256"||std::string(expected)=="512"||std::string(expected)=="4096");expected_pixels=std::stoul(expected);
    texture_output=audit;fs::create_directories(texture_output);PathsSetDataRoot(data);PathsSetSaveRoot(texture_output);ConfigLoad();
    std::string error;ASSERT_TRUE(mods::TextureDiscoveryRecorder().Start(texture_output/"captures",error))<<error;
    dc::test::GfxFixture graphics(640,480,1.0f,dc::test::GfxOptions{.offscreen=true});InputInit();
    InputScript script;std::string input="0\n";for(int i=90;i<18100;i+=100)input+=std::to_string(i)+" cross\n"+std::to_string(i+5)+"\n";
    ASSERT_TRUE(InputScriptParse(input,script,error));InputScriptInstall(std::move(script));ClockSetUnbounded(true);ClockReset();ClockAddPumpHook(TexturePump);
    ASSERT_TRUE(GameSetJump("dungeon:0"));GameSetFastLoad(true);GameSetAutosave(false);GameSetFrameBudget(18500);
    const int result=RunGame(0,nullptr);ClockRemovePumpHook(TexturePump);InputShutdown();mods::TextureDiscoveryRecorder().Stop();
    EXPECT_EQ(result,0);EXPECT_TRUE(completed);EXPECT_TRUE(mods::TextureDiscoveryRecorder().Errors().empty());
}
}
