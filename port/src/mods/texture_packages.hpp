#pragma once
#include "manager.hpp"
#include <functional>
#include "texture_port.hpp"
namespace mods {
std::vector<std::uint8_t> TextureRgbaPixels(const PortDecodedTexture &texture);
std::string TexturePixelIdentity(const PortDecodedTexture &texture);
std::string PackageTextureIdentity(std::string_view name,const PortDecodedTexture &texture);
bool ApplyTextureReplacement(const Manager &manager,std::string_view name,std::string_view identity,PortDecodedTexture &texture,std::string &error);
using TextureUpload = std::function<unsigned(const PortDecodedTexture &,PortTextureOwner,unsigned *)>;
unsigned CreatePackageTexture(const Manager &manager,std::string_view name,std::string_view identity,
                              const PortDecodedTexture &original,PortTextureOwner owner,unsigned *palette,
                              std::string &error,const TextureUpload &upload=PortCreateTexture);
}
