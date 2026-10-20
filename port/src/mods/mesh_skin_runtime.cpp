#include "mesh_skin_runtime.hpp"
#include <map>
#include <algorithm>
#include <cmath>
namespace mods {
namespace {
struct Entry {
    const void *model=nullptr,*rest=nullptr;
    std::size_t source_count=0;
    MeshSkinBinding binding;
    bool begun=false,native_complete=false;
    std::vector<MeshSkinBinding::Point> source_rest,native_pose;
    std::string_view error;
};
std::map<const void *,Entry> bindings;
const void *current_model=nullptr;
}
std::size_t MeshSkinBindingCount() noexcept { return bindings.size(); }
bool AttachMeshSkin(const void *visual,const void *model,std::span<const MeshSkinBinding::Point> source,
                    std::span<const MeshSkinBinding::Point> replacement,std::string &error,std::span<const std::uint32_t> mapping) {
    error.clear();
    if(!visual || !model) {error="mesh binding requires visual and model identity";return false;}
    if(!bindings.contains(visual) && bindings.size()>=256) {error="mesh visual binding capacity reached";return false;}
    std::size_t source_total=source.size(),replacement_total=replacement.size();
    for(const auto &[key,entry]:bindings) if(key!=visual) {source_total+=entry.source_count;replacement_total+=entry.binding.Positions().size();}
    if(source_total>1000000 || replacement_total>1000000) {error="mesh binding point budget exceeded";return false;}
    try {
        Entry next;next.model=model;next.source_count=source.size();next.binding=MeshSkinBinding::Prepare(source,replacement,mapping);
        next.source_rest.assign(source.begin(),source.end());next.native_pose=next.source_rest;
        bindings.insert_or_assign(visual,std::move(next));return true;
    } catch(const std::exception &failure) {error=failure.what();return false;}
}
void DetachMeshSkin(const void *visual) noexcept { if(!bindings.empty()) bindings.erase(visual); }
std::span<const MeshSkinBinding::Vertex> ReadMeshSkin(const void *visual) noexcept {
    const auto it=bindings.find(visual);
    if(it==bindings.end() || !it->second.begun || !it->second.error.empty()) return {};
    return it->second.binding.Positions();
}
std::span<const MeshSkinBinding::Point> ReadNativeMeshPose(const void *visual) noexcept {
    const auto it=bindings.find(visual);if(it==bindings.end()||!it->second.begun||!it->second.native_complete||!it->second.error.empty())return {};
    return it->second.native_pose;
}
std::span<const std::uint32_t> ReadMeshSkinSources(const void *visual) noexcept {
    const auto it=bindings.find(visual);return it==bindings.end()?std::span<const std::uint32_t>{}:it->second.binding.Sources();
}
std::string_view MeshSkinError(const void *visual) noexcept {
    const auto it=bindings.find(visual);return it==bindings.end()?std::string_view{}:it->second.error;
}
void BeginNativeMeshSkin(const void *model,const void *rest_vertices,std::size_t count) noexcept {
    current_model=nullptr;
    if(bindings.empty() || !model) return;
    for(auto &[key,entry]:bindings) if(entry.model==model && entry.error.empty()) {
        if(!rest_vertices || count!=entry.source_count || (entry.rest && entry.rest!=rest_vertices)) {
            entry.error="native mesh rest data changed; rebuild binding";entry.begun=false;continue;
        }
        entry.rest=rest_vertices;entry.binding.BeginPose();entry.begun=true;current_model=model;
        std::copy(entry.source_rest.begin(),entry.source_rest.end(),entry.native_pose.begin());entry.native_complete=true;
    }
}
bool NativeMeshSkinActive() noexcept { return current_model!=nullptr && !bindings.empty(); }
void ApplyNativeMeshBone(std::uint32_t source_vertex,const float base[4][4],const float bone[4][4],const float inverse[4][4],float weight,const float *native_position) noexcept {
    if(!current_model || bindings.empty()) return;
    for(auto &[key,entry]:bindings) if(entry.model==current_model && entry.begun && entry.error.empty()) {
        if((native_position && (!std::isfinite(native_position[0])||!std::isfinite(native_position[1])||!std::isfinite(native_position[2]))) || !entry.binding.ApplyBone(source_vertex,base,bone,inverse,weight)) {
            entry.error="invalid native mesh influence; rebuild binding";entry.begun=false;
        } else if(native_position) {
            std::copy_n(native_position,3,entry.native_pose[source_vertex].begin());
        } else entry.native_complete=false;
    }
}
}
