#include <nlohmann/json.hpp>
#include "mesh_discovery.hpp"
#include "mesh_asset.hpp"
#include "native.hpp"
#include "draw3d.hpp"
#include "platform/files.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
namespace mods {
namespace {
using Json=nlohmann::json;
void Require(bool value,const char *message) {if(!value)throw std::runtime_error(message);}
void CheckPath(const std::filesystem::path &path) {
    for(const auto &part:path) {
        auto name=part.string();for(char &c:name)if(c>='A'&&c<='Z')c+='a'-'A';
        Require(name!="ps2","mesh discovery may not write within a PS2 directory");
    }
}
Json Snapshot(const Draw3DVisual &visual,unsigned native_vertices) {
    Require(!visual.immediate&&native_vertices&&native_vertices<=100000,"unsupported native model vertex count or immediate visual");
    Require(visual.vertices.size()>=3&&visual.vertices.size()<=100000&&visual.strips.size()<=256&&visual.indices.size()<=600000,"unsupported native mesh size");
    Json mesh={{"schema_version",1},{"vertices",Json::array()},{"triangles",Json::array()}};
    unsigned fallback_normals=0,dropped=0;std::set<std::string> textures;
    for(const auto &vertex:visual.vertices) {
        double length=0;for(float n:vertex.normal){Require(std::isfinite(n),"non-finite native normal");length+=double(n)*n;}
        length=std::sqrt(length);std::array<float,3> normal{0,0,1};
        if(length>1e-10)for(unsigned a=0;a<3;++a)normal[a]=float(vertex.normal[a]/length);else ++fallback_normals;
        mesh["vertices"].push_back({vertex.position[0],vertex.position[1],vertex.position[2],normal[0],normal[1],normal[2],vertex.uv[0],vertex.uv[1]});
    }
    for(std::size_t s=0;s<visual.strips.size();++s) {
        const auto &strip=visual.strips[s];
        const auto end=std::find(std::begin(strip.texture_name),std::end(strip.texture_name),'\0');
        if(end!=std::begin(strip.texture_name))textures.emplace(strip.texture_name,end);
        Require(strip.first_index<=visual.indices.size()&&strip.index_count<=visual.indices.size()-strip.first_index&&strip.index_count%3==0,"invalid native strip range");
        for(std::size_t i=strip.first_index;i<strip.first_index+strip.index_count;i+=3) {
            const auto a=visual.indices[i],b=visual.indices[i+1],c=visual.indices[i+2];
            Require(a<visual.vertices.size()&&b<visual.vertices.size()&&c<visual.vertices.size(),"invalid native triangle index");
            const auto *p=visual.vertices[a].position,*q=visual.vertices[b].position,*r=visual.vertices[c].position;
            double u[3],v[3];for(unsigned axis=0;axis<3;++axis){u[axis]=double(q[axis])-p[axis];v[axis]=double(r[axis])-p[axis];}
            const double x=u[1]*v[2]-u[2]*v[1],y=u[2]*v[0]-u[0]*v[2],z=u[0]*v[1]-u[1]*v[0];
            if(a==b||b==c||a==c||x*x+y*y+z*z<=1e-20){++dropped;continue;}
            Require(mesh["triangles"].size()<200000,"native mesh exceeds export triangle limit");
            mesh["triangles"].push_back({s,a,b,c});
        }
    }
    // Exported geometry obeys exactly the same bounds and float32 validation as imported geometry.
    DecodeMeshAsset(mesh.dump());
    return {{"schema_version",1},{"native_vertices",native_vertices},{"textures",textures},{"mesh",std::move(mesh)},
            {"fallback_normals",fallback_normals},{"dropped_degenerate_triangles",dropped}};
}
}
bool MeshDiscovery::Start(const std::filesystem::path &directory,std::string &error) {
    error.clear();
    try {
        Require(!active_,"mesh discovery is already active");Require(!directory.empty(),"mesh discovery needs a new output directory");
        CheckPath(directory);const auto absolute=std::filesystem::absolute(directory).lexically_normal();CheckPath(absolute);
        const auto root=std::filesystem::canonical(absolute.parent_path())/absolute.filename();CheckPath(root);
        Require(FilesCreateDirectory(root)==FilesResult::kWritten,"mesh discovery requires a new directory under an existing parent");
        root_=root;seen_.clear();errors_.clear();bytes_=0;active_=true;return true;
    }catch(const std::exception &failure){error=failure.what();return false;}
}
void MeshDiscovery::Observe(const Draw3DVisual &visual,unsigned native_vertices) {
    if(!active_)return;
    try {
        const auto current=std::filesystem::canonical(root_);CheckPath(current);Require(current==root_,"mesh discovery output directory changed");
        auto text=Snapshot(visual,native_vertices).dump();
        Require(text.size()<=16*1024*1024,"mesh discovery snapshot exceeds 16 MiB");
        const auto digest=Sha256(text);if(seen_.contains(digest))return;
        if(seen_.size()>=256||text.size()>64*1024*1024-bytes_) {active_=false;throw std::runtime_error("mesh discovery stopped at its 256-model/64-MiB output limit");}
        Require(FilesWrite(root_/(digest+".model.json"),text.data(),text.size(),false)==FilesResult::kWritten,"cannot exclusively publish mesh snapshot");
        seen_.insert(digest);bytes_+=text.size();
    }catch(const std::exception &failure) {
        if(errors_.size()<64&&std::ranges::find(errors_,failure.what())==errors_.end()) {
            errors_.push_back(failure.what());std::fprintf(stderr,"mesh discovery: %s\n",failure.what());
        }
    }
}
MeshDiscovery &MeshDiscoveryRecorder() {static MeshDiscovery discovery;return discovery;}
}
