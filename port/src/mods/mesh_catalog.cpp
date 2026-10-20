#include "mesh_catalog.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>
namespace mods {
namespace {
bool EqualTexture(std::string_view left, std::string_view right) noexcept {
    if(left.size()!=right.size())return false;
    for(std::size_t i=0;i<left.size();++i) {
        auto lower=[](char c){return c>='A'&&c<='Z'?char(c+'a'-'A'):c;};
        if(lower(left[i])!=lower(right[i]))return false;
    }
    return true;
}
void Require(bool condition,const char *message) {if(!condition)throw std::runtime_error(message);}
}
bool MeshCatalog::LoadPackage(const Package &package,std::string &error) {
    error.clear();if(!package.Active())return true;
    try {
        Require(!package.id.empty(),"mesh package has no owner ID");
        Require(package.meshes.size()<=16,"mesh package exceeds 16 declarations");
        std::vector<std::shared_ptr<const Definition>> next;
        std::size_t points=0,triangles=0;std::uintmax_t bytes=0,owner_bytes=0;
        for(const auto &definition:definitions_)if(definition->owner!=package.id) {
            next.push_back(definition);points+=definition->asset.vertices.size();triangles+=definition->asset.triangles.size();bytes+=definition->bytes;
        }
        Require(next.size()+package.meshes.size()<=256,"mesh catalog exceeds 256 definitions");
        // Do not resolve a root for packages without mesh assets.
        const auto root=package.meshes.empty()?std::filesystem::path{}:std::filesystem::canonical(package.root);
        for(const auto &target:package.meshes) {
            Require(!target.texture.empty()&&target.texture.size()<=31&&std::ranges::all_of(target.texture,[](char c){
                return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-';
            }),"invalid mesh texture target");
            Require(target.native_vertices<=100000,"invalid mesh native vertex count");
            for(const auto &other:next)Require(!EqualTexture(other->target.texture,target.texture)||
                (other->target.native_vertices&&target.native_vertices&&other->target.native_vertices!=target.native_vertices),"overlapping mesh catalog targets");
            const auto path=std::filesystem::canonical(target.source);
            const auto relative=path.lexically_relative(root);
            Require(!relative.empty()&&!relative.is_absolute()&&*relative.begin()!=".."&&std::filesystem::is_regular_file(path),"mesh file escapes package or is not regular");
            const auto size=std::filesystem::file_size(path);
            Require(size&&size<=16*1024*1024,"mesh file exceeds 16 MiB or is empty");
            owner_bytes+=size;bytes+=size;
            Require(owner_bytes<=64*1024*1024&&bytes<=64*1024*1024,"mesh catalog exceeds 64 MiB encoded budget");
            std::ifstream input(path,std::ios::binary);std::string text(static_cast<std::size_t>(size),'\0');
            Require(bool(input.read(text.data(),static_cast<std::streamsize>(size)))&&input.peek()==std::char_traits<char>::eof(),"mesh file changed or cannot be read");
            auto definition=std::make_shared<Definition>();definition->owner=package.id;definition->target=target;
            definition->asset=DecodeMeshAsset(text);definition->bytes=size;
            points+=definition->asset.vertices.size();triangles+=definition->asset.triangles.size();
            Require(points<=1000000&&triangles<=1000000,"mesh catalog exceeds geometry budget");
            next.push_back(std::move(definition));
        }
        definitions_.swap(next);return true;
    }catch(const std::exception &failure){error=failure.what();return false;}
}
MeshCatalog::Match MeshCatalog::Select(std::span<const std::string_view> textures,unsigned native_vertices) const noexcept {
    Match result;if(!native_vertices)return result;
    for(const auto &definition:definitions_) {
        if(definition->target.native_vertices&&definition->target.native_vertices!=native_vertices)continue;
        if(!std::ranges::any_of(textures,[&](auto name){return EqualTexture(name,definition->target.texture);}))continue;
        if(!result.first)result.first=definition;
        else {result.conflict=definition;return result;}
    }
    return result;
}
void MeshCatalog::Retain(std::span<const std::string> owners) {
    std::erase_if(definitions_,[&](const auto &definition){return std::ranges::find(owners,definition->owner)==owners.end();});
}
}
