#pragma once
#include "manager.hpp"
#include "texture_port.hpp"
namespace mods {
std::string TexturePixelIdentity(const PortDecodedTexture &texture);
std::string PackageTextureIdentity(std::string_view name,const PortDecodedTexture &texture);
bool ApplyTextureReplacement(const Manager &manager,std::string_view name,std::string_view identity,PortDecodedTexture &texture,std::string &error);
void ApplyPackageTexture(std::string_view name,std::string_view identity,PortDecodedTexture &texture);
}
