#include "texture_packages.hpp"
#include "native.hpp"
#include "session.hpp"
#include "platform/png_image.hpp"
#include <cstring>
#include <cstdio>
#include <stdexcept>
namespace mods {
namespace {
std::string Fold(std::string_view value) {std::string out(value);for(char &c:out)if(c>='A'&&c<='Z')c+=32;return out;}
}
std::string TexturePixelIdentity(const PortDecodedTexture &texture) {
    if(texture.levels.empty()||!texture.width||!texture.height)throw std::runtime_error("Empty texture identity");
    const auto pixels=static_cast<std::size_t>(texture.width)*texture.height;
    const bool indexed=texture.format==gfx::TextureFormat::Index8;
    if(pixels>16u*1024u*1024u||texture.levels[0].size()!=pixels*(indexed?1u:4u))throw std::runtime_error("Invalid texture identity dimensions");
    std::string data="chronicle-rgba-v1:"+std::to_string(texture.width)+":"+std::to_string(texture.height)+":";
    data.reserve(data.size()+pixels*4);
    if(indexed)for(auto index:texture.levels[0]) {
        const auto color=texture.palette[index];for(int shift=0;shift<32;shift+=8)data.push_back(static_cast<char>(color>>shift));
    } else data.append(reinterpret_cast<const char *>(texture.levels[0].data()),texture.levels[0].size());
    return Sha256(data);
}
std::string PackageTextureIdentity(std::string_view name,const PortDecodedTexture &texture) {
    const auto &targets=Shared().Active().Textures();if(targets.empty())return {};
    const auto prefix=Fold(name)+":";const auto found=targets.lower_bound(prefix);
    if(found==targets.end()||!found->first.starts_with(prefix))return {};
    try{return TexturePixelIdentity(texture);}catch(...){return {};}
}
bool ApplyTextureReplacement(const Manager &manager,std::string_view name,std::string_view identity,PortDecodedTexture &texture,std::string &error) {
    error.clear();if(identity.empty())return false;
    const auto found=manager.Textures().find(Fold(name)+":"+std::string(identity));if(found==manager.Textures().end())return false;
    const auto &target=found->second;
    try {
        const auto source=std::filesystem::canonical(target.source);const auto relative=source.lexically_relative(target.package_root);
        if(relative.empty()||relative.is_absolute()||*relative.begin()=="..")throw std::runtime_error("Texture source escaped its package");
        int w=0,h=0;std::vector<std::uint8_t> pixels;
        if(!PngLoadRgbaBounded(source,{},w,h,pixels,error))return false;
        PortDecodedTexture replacement;replacement.width=w;replacement.height=h;replacement.levels.push_back(std::move(pixels));
        if(TexturePixelIdentity(replacement)!=target.source_sha256)throw std::runtime_error("Texture source changed after activation; restart to rescan");
        replacement.logical_width=texture.logical_width?texture.logical_width:texture.width;
        replacement.logical_height=texture.logical_height?texture.logical_height:texture.height;
        replacement.has_alpha=texture.has_alpha;
        texture=std::move(replacement);return true;
    }catch(const std::exception &failure){error=failure.what();return false;}
}
unsigned CreatePackageTexture(const Manager &manager,std::string_view name,std::string_view identity,
                              const PortDecodedTexture &original,PortTextureOwner owner,unsigned *palette,
                              std::string &error,const TextureUpload &upload) {
    error.clear();
    if(!identity.empty()) {
        // Only carry metadata into the candidate; keep native pixels and palette intact for fallback.
        PortDecodedTexture candidate;candidate.width=original.width;candidate.height=original.height;
        candidate.logical_width=original.logical_width;candidate.logical_height=original.logical_height;
        candidate.has_alpha=original.has_alpha;
        if(ApplyTextureReplacement(manager,name,identity,candidate,error)) {
            unsigned candidate_palette=0,key=0;
            try {
                key=upload(candidate,owner,&candidate_palette);
                if(key&&PortTextureFromTex0(static_cast<u_long>(key),0).valid) {
                    if(palette)*palette=candidate_palette;
                    return key;
                }
                error="Replacement GPU upload failed";
            }catch(const std::exception &failure){error=std::string("Replacement GPU upload failed: ")+failure.what();}
            if(key)PortReleaseKey(key);
            if(candidate_palette)PortReleaseKey(candidate_palette);
        }
    }
    if(!error.empty())std::fprintf(stderr,"mods: texture %.*s: %s; using original\n",static_cast<int>(name.size()),name.data(),error.c_str());
    return upload(original,owner,palette);
}
}
