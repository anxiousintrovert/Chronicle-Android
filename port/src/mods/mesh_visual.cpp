#include "mesh_visual.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace mods {
namespace {
void Require(bool valid,const char *message) {if(!valid)throw std::runtime_error(message);}
bool Finite(const float *values,unsigned count) {for(unsigned i=0;i<count;++i)if(!std::isfinite(values[i]))return false;return true;}
bool Material(const Draw3DStrip &strip) {return Finite(strip.diffuse,4)&&Finite(strip.ambient,4)&&Finite(strip.specular,4)&&std::isfinite(strip.ground_uv_period);}
}
PreparedMeshVisual PrepareMeshVisual(const Draw3DVisual &source,const MeshAsset &asset) {
    Require(!source.immediate,"per-frame immediate visuals do not support this binding");
    Require(!source.vertices.empty()&&source.vertices.size()<=100000&&!source.strips.empty()&&source.strips.size()<=256,"invalid source visual counts");
    Require(asset.vertices.size()>=3&&asset.vertices.size()<=100000&&!asset.triangles.empty()&&asset.triangles.size()<=200000,"invalid replacement visual counts");
    PreparedMeshVisual result;
    result.visual.vertex_colour=source.vertex_colour;result.visual.strips=source.strips;
    for(const auto &strip:source.strips)Require(Material(strip),"invalid source material");
    for(const auto &vertex:source.vertices) {
        Require(Finite(vertex.position,3),"invalid source vertex");
        result.source_rest.push_back({vertex.position[0],vertex.position[1],vertex.position[2]});
    }
    for(const auto &vertex:asset.vertices) {
        Require(Finite(vertex.data(),8),"invalid replacement vertex");
        for(float value:vertex)Require(std::abs(value)<=1000000,"replacement component exceeds format bound");
        const double length=std::sqrt(double(vertex[3])*vertex[3]+double(vertex[4])*vertex[4]+double(vertex[5])*vertex[5]);
        Require(std::abs(length-1)<=0.001,"replacement normal must have unit length");
        result.replacement_rest.push_back({vertex[0],vertex[1],vertex[2]});
        gfx::Vertex3D output{};std::copy_n(vertex.data(),3,output.position);std::copy_n(vertex.data()+3,3,output.normal);std::copy_n(vertex.data()+6,2,output.uv);
        result.visual.vertices.push_back(output);
    }
    auto binding=MeshSkinBinding::Prepare(result.source_rest,result.replacement_rest);
    result.source_vertices.assign(binding.Sources().begin(),binding.Sources().end());
    for(std::size_t i=0;i<result.visual.vertices.size();++i)
        std::copy_n(source.vertices[result.source_vertices[i]].color,4,result.visual.vertices[i].color);
    std::vector<std::uint32_t> counts(source.strips.size());
    for(const auto &triangle:asset.triangles) {
        Require(triangle.strip<counts.size(),"replacement references absent source material strip");
        for(auto index:triangle.vertices)Require(index<asset.vertices.size(),"replacement triangle index out of range");
        Require(triangle.vertices[0]!=triangle.vertices[1]&&triangle.vertices[0]!=triangle.vertices[2]&&triangle.vertices[1]!=triangle.vertices[2],"replacement triangle repeats vertex");
        const auto &a=asset.vertices[triangle.vertices[0]],&b=asset.vertices[triangle.vertices[1]],&c=asset.vertices[triangle.vertices[2]];
        double u[3],v[3];for(unsigned i=0;i<3;++i){u[i]=double(b[i])-a[i];v[i]=double(c[i])-a[i];}
        const double cross[]{u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
        Require(cross[0]*cross[0]+cross[1]*cross[1]+cross[2]*cross[2]>1e-20,"replacement triangle degenerates at renderer precision");
        counts[triangle.strip]+=3;
    }
    std::uint32_t offset=0;
    for(std::size_t i=0;i<counts.size();++i) {result.visual.strips[i].first_index=offset;result.visual.strips[i].index_count=counts[i];offset+=counts[i];}
    result.visual.indices.resize(offset);std::vector<std::uint32_t> next;
    for(const auto &strip:result.visual.strips)next.push_back(strip.first_index);
    for(const auto &triangle:asset.triangles)for(auto index:triangle.vertices)result.visual.indices[next[triangle.strip]++]=index;
    return result;
}
bool RefreshMeshVisual(PreparedMeshVisual &prepared,const Draw3DVisual &source,std::span<const MeshSkinBinding::Vertex> pose) noexcept {
    auto &out=prepared.visual;
    if(source.immediate||source.vertices.size()!=prepared.source_rest.size()||source.strips.size()!=out.strips.size()||
       prepared.source_vertices.size()!=out.vertices.size()||prepared.replacement_rest.size()!=out.vertices.size()||
       (!pose.empty()&&pose.size()!=out.vertices.size()))return false;
    for(const auto &strip:source.strips)if(!Material(strip))return false;
    for(std::size_t i=0;i<out.vertices.size();++i) {
        if(prepared.source_vertices[i]>=source.vertices.size())return false;
        for(unsigned axis=0;axis<3;++axis) {
            const auto native=prepared.source_vertices[i];
            const float value=pose.empty()?prepared.replacement_rest[i][axis]+(source.vertices[native].position[axis]-prepared.source_rest[native][axis]):pose[i][axis];
            if(!std::isfinite(value))return false;
        }
    }
    for(std::size_t i=0;i<out.vertices.size();++i) {
        const auto native=prepared.source_vertices[i];
        for(unsigned axis=0;axis<3;++axis)out.vertices[i].position[axis]=pose.empty()?prepared.replacement_rest[i][axis]+(source.vertices[native].position[axis]-prepared.source_rest[native][axis]):pose[i][axis];
        std::copy_n(source.vertices[native].color,4,out.vertices[i].color);
    }
    out.vertex_colour=source.vertex_colour;
    for(std::size_t i=0;i<out.strips.size();++i) {
        const auto first=out.strips[i].first_index,count=out.strips[i].index_count;
        out.strips[i]=source.strips[i];out.strips[i].first_index=first;out.strips[i].index_count=count;
    }
    return true;
}
}
