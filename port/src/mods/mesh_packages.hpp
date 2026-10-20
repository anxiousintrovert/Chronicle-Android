#pragma once
#include "mesh_catalog.hpp"
namespace mods {
class MeshPackages {
public:
    MeshPackages() = default;
    MeshPackages(const MeshPackages &) = delete;
    MeshPackages &operator=(const MeshPackages &) = delete;
    bool LoadPackage(const Package &package, std::string &error) {return catalog_.LoadPackage(package,error);}
    bool Active() const noexcept {return catalog_.Size()!=0;}
    void Attach(const void *block,const void *model,const float (*rest)[4],std::size_t count,
                std::span<const std::uint32_t> source_indices);
    void Retain(std::span<const std::string> owners);
    void Clear();
    std::vector<std::string> Diagnostics() const;
private:
    void Report(std::string_view owner,std::string message);
    MeshCatalog catalog_;
    std::vector<std::string> diagnostics_;
};
bool PackageMeshesActive();
void AttachPackageMesh(const void *block,const void *model,const float (*rest)[4],std::size_t count,
                       std::span<const std::uint32_t> source_indices);
}
