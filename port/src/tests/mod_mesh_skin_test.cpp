#include <gtest/gtest.h>
#include "mods/mesh_skin.hpp"
#include <cmath>
#include <cstdlib>
#include <limits>
using Binding=mods::MeshSkinBinding;
namespace {
struct Matrix { float v[4][4]{}; Matrix(){for(unsigned i=0;i<4;++i)v[i][i]=1;} };
TEST(ModMeshSkin, NearestBindingsHaveDeterministicLowestIndexTiesAndNoRng) {
    const Binding::Point source[]{{-1,0,0},{1,0,0},{-1,0,0},{0,2,0}},replacement[]{{0,0,0},{-1,0,0},{0,1.9f,0}};
    srand(81);const int expected=rand();srand(81);
    auto binding=Binding::Prepare(source,replacement);
    EXPECT_EQ((std::vector<std::uint32_t>(binding.Sources().begin(),binding.Sources().end())),(std::vector<std::uint32_t>{0,0,3}));
    EXPECT_EQ(rand(),expected);
}
TEST(ModMeshSkin, SpatialSearchMatchesExhaustiveOracleAcrossAllAxes) {
    std::vector<Binding::Point> source,replacement;
    for(int i=0;i<255;++i) source.push_back({float((i*17)%37),float((i*31)%41),float((i*13)%29)});
    for(int i=0;i<127;++i) replacement.push_back({float((i*7)%37)+0.25f,float((i*11)%41)-0.5f,float((i*19)%29)+0.75f});
    const auto binding=Binding::Prepare(source,replacement);
    for(unsigned i=0;i<replacement.size();++i) {
        double best=INFINITY;unsigned index=0;
        for(unsigned j=0;j<source.size();++j) {double d=0;for(unsigned axis=0;axis<3;++axis){const double delta=double(replacement[i][axis])-source[j][axis];d+=delta*delta;}if(d<best){best=d;index=j;}}
        EXPECT_EQ(binding.Sources()[i],index) << i;
    }
}
TEST(ModMeshSkin, InheritedWeightsAccumulateAndBeginPoseDoesNotDrift) {
    const Binding::Point source[]{{0,0,0},{100,0,0}},replacement[]{{1,0,0},{2,0,0},{99,0,0}};
    auto binding=Binding::Prepare(source,replacement);Matrix base,bone,inverse;bone.v[3][0]=8;
    ASSERT_TRUE(binding.ApplyBone(0,base.v,bone.v,inverse.v,0.25f));
    EXPECT_EQ(binding.Positions()[0][0],3);EXPECT_EQ(binding.Positions()[1][0],4);EXPECT_EQ(binding.Positions()[2][0],99);
    bone.v[3][0]=4;ASSERT_TRUE(binding.ApplyBone(0,base.v,bone.v,inverse.v,0.5f));EXPECT_EQ(binding.Positions()[0][0],5);
    binding.BeginPose();EXPECT_EQ(binding.Positions()[0][0],1);EXPECT_EQ(binding.Positions()[2][0],99);
    bone.v[3][0]=8;ASSERT_TRUE(binding.ApplyBone(0,base.v,bone.v,inverse.v,0.25f));EXPECT_EQ(binding.Positions()[0][0],3);
}
TEST(ModMeshSkin, RotationAndInverseUseReplacementRestPositionNotSourcePosition) {
    const Binding::Point source[]{{1,0,0}},replacement[]{{2,0,0}};
    auto binding=Binding::Prepare(source,replacement);Matrix base,bone,inverse;
    bone.v[0][0]=0;bone.v[0][1]=1;bone.v[1][0]=-1;bone.v[1][1]=0;inverse.v[3][0]=3;
    ASSERT_TRUE(binding.ApplyBone(0,base.v,bone.v,inverse.v,1));
    EXPECT_EQ(binding.Positions()[0],(Binding::Vertex{3,2,0,1}));
}
TEST(ModMeshSkin, InvalidAndOverflowingInfluencesCannotPartiallyChangePose) {
    const Binding::Point source[]{{0,0,0}},replacement[]{{1,0,0},{1000000,0,0}};
    auto binding=Binding::Prepare(source,replacement);Matrix base,bone,inverse;
    const auto before=std::vector<Binding::Vertex>(binding.Positions().begin(),binding.Positions().end());
    EXPECT_FALSE(binding.ApplyBone(1,base.v,bone.v,inverse.v,1));
    EXPECT_FALSE(binding.ApplyBone(0,base.v,bone.v,inverse.v,-1));
    bone.v[0][0]=std::numeric_limits<float>::infinity();EXPECT_FALSE(binding.ApplyBone(0,base.v,bone.v,inverse.v,1));
    bone.v[0][0]=std::numeric_limits<float>::max()/2;EXPECT_FALSE(binding.ApplyBone(0,base.v,bone.v,inverse.v,1));
    EXPECT_EQ(std::vector<Binding::Vertex>(binding.Positions().begin(),binding.Positions().end()),before);
    bone=Matrix{};bone.v[3][0]=4;ASSERT_TRUE(binding.ApplyBone(0,base.v,bone.v,inverse.v,0.5f));EXPECT_EQ(binding.Positions()[0][0],3);
}
TEST(ModMeshSkin, PathologicalNearestSearchStopsAtItsPreparationBudget) {
    std::vector<Binding::Point> source(5000,Binding::Point{0,0,0}),replacement(5001,Binding::Point{0,0,1});
    try { auto binding=Binding::Prepare(source,replacement); FAIL() << "unbounded ambiguous search accepted"; }
    catch(const std::runtime_error &error) { EXPECT_NE(std::string(error.what()).find("work budget"),std::string::npos); }
}
TEST(ModMeshSkin, RejectsInvalidPointsAndKeepsCopiedBindingsIndependent) {
    const Binding::Point source[]{{0,0,0}},replacement[]{{1,0,0}};
    EXPECT_ANY_THROW(Binding::Prepare({},replacement));
    const Binding::Point invalid[]{{NAN,0,0}};EXPECT_ANY_THROW(Binding::Prepare(source,invalid));
    auto original=Binding::Prepare(source,replacement),copy=original;Matrix base,bone,inverse;bone.v[3][0]=5;
    ASSERT_TRUE(copy.ApplyBone(0,base.v,bone.v,inverse.v,1));EXPECT_EQ(copy.Positions()[0][0],6);EXPECT_EQ(original.Positions()[0][0],1);
}
}
