#pragma once
#include <filesystem>
#include <set>
#include <string>
#include <vector>
struct PortDecodedTexture;
namespace mods {
class TextureDiscovery {
public:
    bool Start(const std::filesystem::path &directory,std::string &error);
    void Stop() noexcept {active_=false;}
    void Observe(std::string_view name,const PortDecodedTexture &texture);
    bool Active() const noexcept {return active_;}
    std::size_t Count() const noexcept {return seen_.size();}
    const std::vector<std::string> &Errors() const noexcept {return errors_;}
private:
    bool active_=false;
    std::filesystem::path root_;
    std::set<std::string> seen_;
    std::vector<std::string> errors_;
    std::size_t bytes_=0;
};
TextureDiscovery &TextureDiscoveryRecorder();
}
