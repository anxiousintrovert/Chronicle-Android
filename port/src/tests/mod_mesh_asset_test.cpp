#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "mods/mesh_asset.hpp"
#include <cstdlib>
using Json=nlohmann::json;
namespace {
Json Triangle() { return {{"schema_version",1},{"vertices",{{0,0,0,0,0,1,0,0},{1,0,0,0,0,1,1,0},{0,1,0,0,0,1,0,1}}},{"triangles",{{7,0,1,2}}}}; }
TEST(ModMeshAsset, DecodesDetachedGeometryWithoutTouchingRngOrInput) {
    auto text=Triangle().dump();const auto before=text;
    srand(27);const int next=rand();srand(27);
    const auto mesh=mods::DecodeMeshAsset(text);
    ASSERT_EQ(mesh.vertices.size(),3u);ASSERT_EQ(mesh.triangles.size(),1u);
    EXPECT_EQ(mesh.triangles[0].strip,7u);EXPECT_EQ(mesh.triangles[0].vertices,(std::array<std::uint32_t,3>{0,1,2}));
    EXPECT_EQ(mesh.vertices[1][6],1);EXPECT_EQ(text,before);EXPECT_EQ(rand(),next);
}
TEST(ModMeshAsset, RejectsAmbiguousObjectsNestingBooleansAndOutOfRangeFields) {
    for(const char *text:{"", "[]", "{", "{\"schema_version\":1,\"schema_version\":1,\"vertices\":[],\"triangles\":[]}", "{\"vertices\":{}}", "[[[[[]]]]]"}) EXPECT_ANY_THROW(mods::DecodeMeshAsset(text));
    for(int kind=0;kind<10;++kind) {
        auto value=Triangle();
        switch(kind) {
        case 0:value["schema_version"]=true;break;
        case 1:value["extra"]=1;break;
        case 2:value["triangles"][0][0]=256;break;
        case 3:value["triangles"][0][1]=true;break;
        case 4:value["triangles"][0][1]=-1;break;
        case 5:value["triangles"][0][1]=3;break;
        case 6:value["vertices"][0][5]=2;break;
        case 7:value["vertices"][0][0]=1000001;break;
        case 8:value["vertices"][0][0]=true;break;
        case 9:value["triangles"][0][2]=0;break;
        }
        EXPECT_ANY_THROW(mods::DecodeMeshAsset(value.dump())) << kind;
    }
}
TEST(ModMeshAsset, RejectsGeometryThatCollapsesAtRendererFloatPrecision) {
    auto value=Triangle();value["vertices"][0][0]=1000000.0;value["vertices"][1][0]=999999.999;value["vertices"][2][0]=1000000.0;
    EXPECT_ANY_THROW(mods::DecodeMeshAsset(value.dump()));
    value["vertices"][1][0]=999999.0;EXPECT_NO_THROW(mods::DecodeMeshAsset(value.dump()));
}
TEST(ModMeshAsset, RejectsOversizedInputAndPreservesPerTriangleMaterialOrdering) {
    EXPECT_ANY_THROW(mods::DecodeMeshAsset(std::string(16*1024*1024+1,' ')));
    auto value=Triangle();value["triangles"].push_back({2,2,1,0});
    const auto mesh=mods::DecodeMeshAsset(value.dump());ASSERT_EQ(mesh.triangles.size(),2u);
    EXPECT_EQ(mesh.triangles[1].strip,2u);EXPECT_EQ(mesh.triangles[1].vertices,(std::array<std::uint32_t,3>{2,1,0}));
}
}
