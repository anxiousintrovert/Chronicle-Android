#include "chronicle/dungeon_transport.hpp"

#include <array>
#include <cstring>
#include <stdexcept>

#include "chronicle/dungeon_limits.hpp"
#include "mods/native.hpp"

namespace {
struct Relocation {
    std::size_t offset;
    float       value;
};

struct Script {
    const char                 *sha256;
    std::span<const Relocation> relocations;
};

// Reviewed float-constant operands in each PAL transport program's call graph.
// Full-file fingerprints prevent applying operand offsets to another script.
constexpr Relocation cave[]{
    {371036, 3040},
    {371048, 3010},
    {371468, 3010},
    {371528, 3010},
    {371672, 3040},
    {371816, 3040},
    {371900, 3040}
};
constexpr Relocation forest[]{
    {192788, 3050},
    {192800, 3010},
    {193268, 3026},
    {193328, 3030},
    {193400, 3050},
    {193592, 3050}
};
constexpr Relocation ship[]{
    {140344, 3050},
    {141088, 3050},
    {141100, 3005},
    {141376, 3005},
    {141436, 3005},
    {141556, 3050}
};
constexpr Relocation temple[]{
    {233124, 3040},
    {233136, 3030},
    {233556, 3030},
    {233616, 3030},
    {233760, 3040},
    {233904, 3040},
    {233988, 3040}
};
constexpr Relocation gallery[]{
    {171784, 3050},
    {171796, 3010},
    {172216, 3010},
    {172276, 3010},
    {172420, 3050}
};
const std::array<Script, 7> scripts{
    {{"1a60897a9afccc9de5a49f916042fb4f412a24bee86f5dd28939f9d7310925e4", cave},
     {"b2d09e0a836038b29ea4d9cfed169dcedd910041e97ec4b7a6a4b2337f663c25", forest},
     {"f2d76c094532d9780248e15360ed8e6185ba8c5711ff76619ab7c6bf8ea10f82", ship},
     {"cb890e742e907e908f51f9683734b75ecf07d8cb98a724a2c39429b540b6ba6f", temple},
     {"8c2ccb5ae173ccb05669e07cff54d69d14e3aba481aaf260907259e42627b5bc", {}},
     {"5b7e9024e5c66aee730183057e9cc548b46090dce3c5ed478da6ad7af58b7cef", gallery},
     {"bd1aa0b062160f770b4b997cda4a89d20c554f8dec6ec2a724ccfb460dbf9922", {}}}
};
} // namespace

std::vector<char> ExpandedTransportScript(std::span<const char> script, int dungeon,
                                          int front_extent, int back_extent) {
    using namespace chronicle::dungeon;
    if (front_extent == RetailGridExtent && back_extent == RetailGridExtent) {
        return {};
    }
    if (front_extent != GridStride || back_extent != GridStride || dungeon < 0 || dungeon >= 7) {
        throw std::runtime_error("expanded transport requires matching supported floor extents");
    }
    const auto &known = scripts[dungeon];
    if (mods::Sha256(std::string_view(script.data(), script.size())) != known.sha256) {
        throw std::runtime_error("expanded transport script is not a verified built-in PAL program");
    }
    if (known.relocations.empty()) {
        return {};
    }
    for (const auto &patch : known.relocations) {
        if (patch.offset > script.size() || script.size() - patch.offset < sizeof(float)) {
            throw std::runtime_error("truncated expanded transport operand");
        }
        float value;
        std::memcpy(&value, script.data() + patch.offset, sizeof(value));
        if (value != patch.value) {
            throw std::runtime_error("unexpected expanded transport operand");
        }
    }
    std::vector<char> result(script.begin(), script.end());
    constexpr float   offset = (GridStride - RetailGridExtent) * 160.0f;
    for (const auto &patch : known.relocations) {
        const float value = patch.value + offset;
        std::memcpy(result.data() + patch.offset, &value, sizeof(value));
    }
    return result;
}
