#include "mesh_skin_runtime.hpp"
#include <map>
namespace mods {
namespace {
struct Entry {
    const void *model=nullptr,*rest=nullptr;
    std::size_t source_count=0;
    MeshSkinBinding binding;
    bool begun=false;
    std::string_view error;
};
std::map<const void *,Entry> bindings;
const void *current_model=nullptr;
}
std::size_t MeshSkinBindingCount() noexcept { return bindings.size(); }
bool AttachMeshSkin(const void *visual,const void *model,std::span<const MeshSkinBinding::Point> source,
                    std::span<const MeshSkinBinding::Point> replacement,std::string &error) {
    error.clear();
    if(!visual || !model) {error="mesh binding requires visual and model identity";return false;}
    if(!bindings.contains(visual) && bindings.size()>=256) {error="mesh visual binding capacity reached";return false;}
    std::size_t source_total=source.size(),replacement_total=replacement.size();
    for(const auto &[key,entry]:bindings) if(key!=visual) {source_total+=entry.source_count;replacement_total+=entry.binding.Positions().size();}
    if(source_total>1000000 || replacement_total>1000000) {error="mesh binding point budget exceeded";return false;}
    try {
        Entry next;next.model=model;next.source_count=source.size();next.binding=MeshSkinBinding::Prepare(source,replacement);
        bindings.insert_or_assign(visual,std::move(next));return true;
    } catch(const std::exception &failure) {error=failure.what();return false;}
}
void DetachMeshSkin(const void *visual) noexcept { if(!bindings.empty()) bindings.erase(visual); }
std::span<const MeshSkinBinding::Vertex> ReadMeshSkin(const void *visual) noexcept {
    const auto it=bindings.find(visual);
    if(it==bindings.end() || !it->second.begun || !it->second.error.empty()) return {};
    return it->second.binding.Positions();
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
    }
}
bool NativeMeshSkinActive() noexcept { return current_model!=nullptr && !bindings.empty(); }
void ApplyNativeMeshBone(std::uint32_t source_vertex,const float base[4][4],const float bone[4][4],const float inverse[4][4],float weight) noexcept {
    if(!current_model || bindings.empty()) return;
    for(auto &[key,entry]:bindings) if(entry.model==current_model && entry.begun && entry.error.empty()) {
        if(!entry.binding.ApplyBone(source_vertex,base,bone,inverse,weight)) {
            entry.error="invalid native mesh influence; rebuild binding";entry.begun=false;
        }
    }
}
}
