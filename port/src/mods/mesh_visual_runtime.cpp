#include "mesh_visual_runtime.hpp"
#include "mesh_skin_runtime.hpp"
#include <map>
#include <algorithm>
namespace mods {
namespace {
struct Entry {const void *block=nullptr;PreparedMeshVisual prepared;std::vector<MeshSkinBinding::Vertex> pose;std::string_view error;};
std::map<const Draw3DVisual *,Entry> visuals;
}
bool AttachReplacementVisual(const void *block,const void *model,std::span<const MeshSkinBinding::Point> native_rest,
                             std::span<const std::uint32_t> flat_native_indices,const MeshAsset &asset,std::string &error) {
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
        Entry next;next.block=block;next.prepared=PrepareMeshVisual(*source,asset);next.pose.resize(asset.vertices.size());
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
}
}
