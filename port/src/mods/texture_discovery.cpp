#include <nlohmann/json.hpp>
#include "texture_discovery.hpp"
#include "texture_packages.hpp"
#include "native.hpp"
#include "platform/files.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
namespace mods {
namespace {
using Json=nlohmann::json;
void Require(bool value,const char *message) {if(!value)throw std::runtime_error(message);}
void CheckPath(const std::filesystem::path &path) {
    for(const auto &part:path) {
        auto name=part.string();for(char &c:name)if(c>='A'&&c<='Z')c+='a'-'A';
        Require(name!="ps2","texture discovery may not write within a PS2 directory");
    }
}
// Keep upstream atomic publication while allowing its temporary-file suffix on
// Windows paths whose final snapshot already approaches MAX_PATH.
std::filesystem::path PublicationPath(std::filesystem::path path) {
#ifdef _WIN32
    const auto native=path.make_preferred().native();
    if(native.starts_with(LR"(\\?\)"))return path;
    if(native.starts_with(LR"(\\)"))return std::filesystem::path(std::wstring(LR"(\\?\UNC\)")+native.substr(2));
    return std::filesystem::path(std::wstring(LR"(\\?\)")+native);
#else
    return path;
#endif
}
}
bool TextureDiscovery::Start(const std::filesystem::path &directory,std::string &error) {
    error.clear();
    try {
        Require(!active_,"texture discovery is already active");Require(!directory.empty(),"texture discovery needs a new output directory");
        CheckPath(directory);const auto absolute=std::filesystem::absolute(directory).lexically_normal();CheckPath(absolute);
        const auto root=std::filesystem::canonical(absolute.parent_path())/absolute.filename();CheckPath(root);
        Require(FilesCreateDirectory(PublicationPath(root))==FilesResult::kWritten,"texture discovery requires a new directory under an existing parent");
        root_=root;seen_.clear();errors_.clear();bytes_=0;active_=true;return true;
    }catch(const std::exception &failure){error=failure.what();return false;}
}
void TextureDiscovery::Observe(std::string_view name,const PortDecodedTexture &texture) {
    if(!active_)return;
    try {
        const auto current=std::filesystem::canonical(root_);CheckPath(current);Require(current==root_,"texture discovery output directory changed");
        std::string target(name);for(char &c:target)if(c>='A'&&c<='Z')c+=32;
        Require(!target.empty()&&target.size()<=31&&std::ranges::all_of(target,[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-';}),"unsupported texture target name");
        const auto digest=TexturePixelIdentity(texture);const auto file_id=Sha256(target+":"+digest);
        if(seen_.contains(file_id))return;
        const auto rgba=TextureRgbaPixels(texture);
        auto text=Json{{"schema_version",1},{"name",target},{"width",texture.width},{"height",texture.height},{"original_sha256",digest},{"encoding","rgba8"}}.dump()+"\n";
        text.append(reinterpret_cast<const char *>(rgba.data()),rgba.size());
        if(seen_.size()>=2048||text.size()>256u*1024u*1024u-bytes_) {active_=false;throw std::runtime_error("texture discovery stopped at its 2048-texture/256-MiB limit");}
        Require(FilesWrite(PublicationPath(root_/(file_id+".texture")),text.data(),text.size(),false)==FilesResult::kWritten,"cannot exclusively publish texture snapshot");
        seen_.insert(file_id);bytes_+=text.size();
    }catch(const std::exception &failure) {
        if(errors_.size()<64&&std::ranges::find(errors_,failure.what())==errors_.end()) {errors_.push_back(failure.what());std::fprintf(stderr,"texture discovery: %s\n",failure.what());}
    }
}
TextureDiscovery &TextureDiscoveryRecorder(){static TextureDiscovery discovery;return discovery;}
}
