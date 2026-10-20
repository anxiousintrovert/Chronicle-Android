#include "mesh_visual_runtime.hpp"
#include "mesh_skin_runtime.hpp"
#include <map>
#include <algorithm>
#include <cmath>
#include "visualvu1.hpp"
namespace mods {
namespace {
struct Bounds {
    MeshSkinBinding::Point low{},high{};
};
Bounds Measure(const Draw3DVisual &visual) noexcept {
    Bounds result;
    std::copy_n(visual.vertices.front().position,3,result.low.begin());result.high=result.low;
    for(const auto &vertex:visual.vertices)for(unsigned axis=0;axis<3;++axis) {
        result.low[axis]=std::min(result.low[axis],vertex.position[axis]);
        result.high[axis]=std::max(result.high[axis],vertex.position[axis]);
    }
    return result;
}
struct Entry {std::string owner;const void *context=nullptr;const void *block=nullptr;const void *model=nullptr;Bounds current,previous;PreparedMeshVisual prepared;std::vector<MeshSkinBinding::Vertex> pose;std::string_view error;};
std::map<const Draw3DVisual *,Entry> visuals;
}
bool AttachReplacementVisual(const void *block,const void *model,std::span<const MeshSkinBinding::Point> native_rest,
                             std::span<const std::uint32_t> flat_native_indices,const MeshAsset &asset,std::string &error,std::string_view owner,const void *context) {
    error.clear();const auto *source=Draw3DFindVisual(block);
    if(!source||source->immediate||flat_native_indices.size()!=source->vertices.size()||native_rest.empty()||
       std::ranges::any_of(flat_native_indices,[&](auto i){return i>=native_rest.size();})) {error="invalid registered native visual mapping";return false;}
    if(!visuals.contains(source)&&visuals.size()>=256) {error="replacement visual capacity reached";return false;}
    if(asset.vertices.size()>100000||asset.triangles.size()>200000){error="replacement geometry exceeds per-asset limits";return false;}
    std::size_t points=asset.vertices.size(),source_points=source->vertices.size(),indices=asset.triangles.size()*3;
    for(const auto &[key,entry]:visuals)if(key!=source){points+=entry.prepared.visual.vertices.size();source_points+=entry.prepared.source_rest.size();indices+=entry.prepared.visual.indices.size();}
    if(points>1000000||source_points>1000000||indices>3000000){error="replacement visual point/index budget exceeded";return false;}
    gfx::MeshHandle created=gfx::kNullMesh;
    try {
        Entry next;next.owner=owner;next.context=context;next.block=block;next.model=model;next.prepared=PrepareMeshVisual(*source,asset);next.current=next.previous=Measure(next.prepared.visual);next.pose.resize(asset.vertices.size());
        std::vector<std::uint32_t> mapping;mapping.reserve(asset.vertices.size());
        for(auto flat:next.prepared.source_vertices)mapping.push_back(flat_native_indices[flat]);
        created=gfx::CreateMesh(next.prepared.visual.vertices,next.prepared.visual.indices);
        if(created==gfx::kNullMesh){error="replacement mesh upload failed";return false;}
        next.prepared.visual.mesh=created;
        std::map<const Draw3DVisual *,Entry> staging;staging.emplace(source,std::move(next));auto node=staging.extract(staging.begin());
        if(!AttachMeshSkin(block,model,native_rest,node.mapped().prepared.replacement_rest,error,mapping)){gfx::DestroyMesh(created);return false;}
        auto old=visuals.extract(source);visuals.insert(std::move(node));created=gfx::kNullMesh;
        if(!old.empty())gfx::DestroyMesh(old.mapped().prepared.visual.mesh);
        return true;
    }catch(const std::exception &failure){if(created!=gfx::kNullMesh)gfx::DestroyMesh(created);error=failure.what();return false;}
}
void RetainReplacementOwners(const void *context,std::span<const std::string> owners) {
    for(auto it=visuals.begin();it!=visuals.end();) {
        const auto *source=it->first;const auto &entry=it->second;++it;
        if(entry.context==context&&!entry.owner.empty()&&std::ranges::find(owners,entry.owner)==owners.end())DetachReplacementVisual(source);
    }
}
std::vector<std::string> ReplacementDiagnostics(const void *context) {
    std::vector<std::string> result;
    for(const auto &[source,entry]:visuals)if(entry.context==context&&!entry.owner.empty()&&!entry.error.empty()) {
        auto message=entry.owner+": mesh refresh: "+std::string(entry.error);
        if(std::ranges::find(result,message)==result.end())result.push_back(std::move(message));
        if(result.size()>=64)break;
    }
    return result;
}
bool ReplacementVisualCorners(CVisualVu1 &visual,const float native[8][4],float output[8][4]) noexcept {
    // Avoid even querying native model identity on the dormant path. Union all
    // buffers for a model: MDT draw selects its double buffer after frame culling.
    if(visuals.empty())return false;
    const void *model=visual.GetMDTDataAddress();if(!model)return false;
    Bounds bounds;bool found=false;
    for(const auto &[source,entry]:visuals) {
        if(entry.model!=model||!entry.error.empty())continue;
        if(!found){bounds=entry.current;found=true;}
        for(unsigned axis=0;axis<3;++axis) {
            bounds.low[axis]=std::min({bounds.low[axis],entry.current.low[axis],entry.previous.low[axis]});
            bounds.high[axis]=std::max({bounds.high[axis],entry.current.high[axis],entry.previous.high[axis]});
        }
    }
    if(!found)return false;
    for(unsigned i=0;i<8;++i)for(unsigned axis=0;axis<3;++axis) {
        if(!std::isfinite(native[i][axis]))return false;
        bounds.low[axis]=std::min(bounds.low[axis],native[i][axis]);
        bounds.high[axis]=std::max(bounds.high[axis],native[i][axis]);
    }
    for(unsigned i=0;i<8;++i) {
        for(unsigned axis=0;axis<3;++axis)output[i][axis]=(i&(1u<<axis))?bounds.low[axis]:bounds.high[axis];
        output[i][3]=1;
    }
    return true;
}
void DetachReplacementVisual(const Draw3DVisual *source) {
    if(visuals.empty())return;const auto it=visuals.find(source);if(it==visuals.end())return;
    DetachMeshSkin(it->second.block);gfx::DestroyMesh(it->second.prepared.visual.mesh);visuals.erase(it);
}
const Draw3DVisual &ResolveReplacementVisual(const Draw3DVisual &source) noexcept {
    if(visuals.empty())return source;const auto it=visuals.find(&source);
    return it==visuals.end()||!it->second.error.empty()?source:it->second.prepared.visual;
}
std::string_view ReplacementVisualError(const Draw3DVisual &source) noexcept {
    const auto it=visuals.find(&source);return it==visuals.end()?std::string_view{}:it->second.error;
}
void RefreshReplacementVisual(const Draw3DVisual &source) {
    if(visuals.empty())return;const auto it=visuals.find(&source);if(it==visuals.end()||!it->second.error.empty())return;auto &entry=it->second;
    const auto failure=MeshSkinError(entry.block);if(!failure.empty()){entry.error=failure;return;}
    const auto skin=ReadMeshSkin(entry.block);
    if(!skin.empty()) {
        const auto native=ReadNativeMeshPose(entry.block);const auto indices=ReadMeshSkinSources(entry.block);
        if(skin.size()!=entry.pose.size()||indices.size()!=entry.pose.size()||native.empty()){entry.error="native skin correspondence is unavailable";return;}
        if(source.vertices.size()!=entry.prepared.source_rest.size()){entry.error="native visual topology changed";return;}
        for(std::size_t i=0;i<skin.size();++i) {
            if(indices[i]>=native.size()){entry.error="native skin index changed";return;}
            entry.pose[i]=skin[i];const auto &actual=source.vertices[entry.prepared.source_vertices[i]];
            for(unsigned axis=0;axis<3;++axis)entry.pose[i][axis]+=actual.position[axis]-native[indices[i]][axis];
        }
    }
    if(!RefreshMeshVisual(entry.prepared,source,skin.empty()?std::span<const MeshSkinBinding::Vertex>{}:std::span<const MeshSkinBinding::Vertex>{entry.pose})||
       !gfx::UpdateMeshVertices(entry.prepared.visual.mesh,0,entry.prepared.visual.vertices))entry.error="replacement visual refresh failed";
    else {entry.previous=entry.current;entry.current=Measure(entry.prepared.visual);}
}
}
