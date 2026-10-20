#include "mesh_packages.hpp"
#include "mesh_visual_runtime.hpp"
#include "runtime.hpp"
#include <algorithm>
#include <array>
namespace mods {
void MeshPackages::Report(std::string_view owner,std::string message) {
    if(diagnostics_.size()>=64)return;
    message=std::string(owner)+": mesh: "+message;
    if(std::ranges::find(diagnostics_,message)==diagnostics_.end())diagnostics_.push_back(std::move(message));
}
void MeshPackages::Attach(const void *block,const void *model,const float (*rest)[4],std::size_t count,
                          std::span<const std::uint32_t> source_indices) {
    if(!Active())return;
    const auto *visual=Draw3DFindVisual(block);
    if(!visual||count==0||count>100000||visual->strips.size()>256)return;
    std::array<std::string_view,256> names;std::size_t size=0;
    for(const auto &strip:visual->strips) {
        const auto end=std::find(std::begin(strip.texture_name),std::end(strip.texture_name),'\0');
        names[size++]={strip.texture_name,static_cast<std::size_t>(end-std::begin(strip.texture_name))};
    }
    const auto match=catalog_.Select({names.data(),size},static_cast<unsigned>(count));
    if(!match.first)return;
    if(!match.Unique()) {
        const auto detail="ambiguous native model: '"+match.first->target.texture+"' from '"+match.first->owner+
            "' and '"+match.conflict->target.texture+"' from '"+match.conflict->owner+"'; original visual retained";
        Report(match.first->owner,detail);Report(match.conflict->owner,detail);return;
    }
    try {
        if(!rest||source_indices.size()!=visual->vertices.size()) {Report(match.first->owner,"native source mapping is unavailable; original visual retained");return;}
        std::vector<MeshSkinBinding::Point> points;points.reserve(count);
        for(std::size_t i=0;i<count;++i)points.push_back({rest[i][0],rest[i][1],rest[i][2]});
        std::string error;
        if(!AttachReplacementVisual(block,model,points,source_indices,match.first->asset,error,match.first->owner,this))
            Report(match.first->owner,error+"; original visual retained");
    }catch(const std::exception &failure){Report(match.first->owner,std::string(failure.what())+"; original visual retained");}
}
void MeshPackages::Retain(std::span<const std::string> owners) {
    catalog_.Retain(owners);RetainReplacementOwners(this,owners);
}
void MeshPackages::Clear() {catalog_.Clear();RetainReplacementOwners(this,{});diagnostics_.clear();}
std::vector<std::string> MeshPackages::Diagnostics() const {
    auto result=diagnostics_;auto refresh=ReplacementDiagnostics(this);
    result.insert(result.end(),refresh.begin(),refresh.end());return result;
}
bool PackageMeshesActive() {return Scripts().Meshes().Active();}
void AttachPackageMesh(const void *block,const void *model,const float (*rest)[4],std::size_t count,
                       std::span<const std::uint32_t> source_indices) {
    Scripts().Meshes().Attach(block,model,rest,count,source_indices);
}
}
