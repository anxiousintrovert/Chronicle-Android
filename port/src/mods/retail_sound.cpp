#include "retail_sound.hpp"
#include <span>
#include <array>
#include "audio/hdbank.hpp"
extern SND_SE_INFO cap0[92];
extern SND_SE_INFO cap1[73];
extern SND_SE_INFO cap2[101];
extern SND_SE_INFO cap3[44];
extern SND_SE_INFO cap4[36];
extern SND_SE_INFO cap10[85];
extern SND_SE_INFO cap11[101];
extern SND_SE_INFO cap12[31];
extern SND_SE_INFO cap13[28];
extern SND_SE_INFO cap14[36];
extern SND_SE_INFO cap15[14];
extern SND_SE_INFO cap16[92];
extern SND_SE_INFO cap17[11];
extern SND_SE_INFO cap20[76];
extern SND_SE_INFO cap21[2];
extern SND_SE_INFO cap22[95];
extern SND_SE_INFO cap25[29];
extern SND_SE_INFO cap26[2];
extern SND_SE_INFO cap27[101];
extern SND_SE_INFO cap30[2];
extern SND_SE_INFO cap31[15];
extern SND_SE_INFO cap32[95];
extern SND_SE_INFO cap35[2];
extern SND_SE_INFO cap36[2];
extern SND_SE_INFO cap37[95];
extern SND_SE_INFO cap40[32];
extern SND_SE_INFO cap41[2];
extern SND_SE_INFO cap42[95];
extern SND_SE_INFO cap45[2];
extern SND_SE_INFO cap46[2];
extern SND_SE_INFO cap47[95];
extern SND_SE_INFO cap50[43];
extern SND_SE_INFO cap51[75];
extern SND_SE_INFO cap52[79];
extern SND_SE_INFO cap53[22];
extern SND_SE_INFO cap54[35];
extern SND_SE_INFO cap55[47];
extern SND_SE_INFO cap56[55];
extern SND_SE_INFO cap57[79];
extern SND_SE_INFO cap58[99];
extern SND_SE_INFO cap59[2];
extern SND_SE_INFO cap60[2];
extern SND_SE_INFO cap61[2];
extern SND_SE_INFO cap62[2];
extern SND_SE_INFO cap63[2];
extern SND_SE_INFO cap64[2];
extern SND_SE_INFO cap65[2];
extern SND_SE_INFO voice0[54];
extern SND_SE_INFO voice1[54];
extern SND_SE_INFO voice2[54];
extern SND_SE_INFO voice3[54];
extern SND_SE_INFO voice4[54];
extern SND_SE_INFO voice5[64];
extern SND_SE_INFO voice6[7];

namespace mods {
namespace {
std::span<const SND_SE_INFO> Known(const SND_SE_INFO *table) {
    if (table == cap0) return cap0;
    if (table == cap1) return cap1;
    if (table == cap2) return cap2;
    if (table == cap3) return cap3;
    if (table == cap4) return cap4;
    if (table == cap10) return cap10;
    if (table == cap11) return cap11;
    if (table == cap12) return cap12;
    if (table == cap13) return cap13;
    if (table == cap14) return cap14;
    if (table == cap15) return cap15;
    if (table == cap16) return cap16;
    if (table == cap17) return cap17;
    if (table == cap20) return cap20;
    if (table == cap21) return cap21;
    if (table == cap22) return cap22;
    if (table == cap25) return cap25;
    if (table == cap26) return cap26;
    if (table == cap27) return cap27;
    if (table == cap30) return cap30;
    if (table == cap31) return cap31;
    if (table == cap32) return cap32;
    if (table == cap35) return cap35;
    if (table == cap36) return cap36;
    if (table == cap37) return cap37;
    if (table == cap40) return cap40;
    if (table == cap41) return cap41;
    if (table == cap42) return cap42;
    if (table == cap45) return cap45;
    if (table == cap46) return cap46;
    if (table == cap47) return cap47;
    if (table == cap50) return cap50;
    if (table == cap51) return cap51;
    if (table == cap52) return cap52;
    if (table == cap53) return cap53;
    if (table == cap54) return cap54;
    if (table == cap55) return cap55;
    if (table == cap56) return cap56;
    if (table == cap57) return cap57;
    if (table == cap58) return cap58;
    if (table == cap59) return cap59;
    if (table == cap60) return cap60;
    if (table == cap61) return cap61;
    if (table == cap62) return cap62;
    if (table == cap63) return cap63;
    if (table == cap64) return cap64;
    if (table == cap65) return cap65;
    if (table == voice0) return voice0;
    if (table == voice1) return voice1;
    if (table == voice2) return voice2;
    if (table == voice3) return voice3;
    if (table == voice4) return voice4;
    if (table == voice5) return voice5;
    if (table == voice6) return voice6;
    return {};
}
std::optional<SND_SE_INFO> Entry(std::span<const SND_SE_INFO> table, int index, bool sentinel, std::string &error) {
    if (index < 0 || static_cast<std::size_t>(index) >= table.size()) {
        error = "sound effect is outside the selected retail table"; return {};
    }
    if (sentinel) for (int i = 0; i <= index; ++i) {
        if (table[i].bank == -128) { error = "sound effect is past the retail table terminator"; return {}; }
    }
    const auto info = table[index];
    if (info.bank < 0 || info.prog < 0) { error = "sound effect is an unused retail table entry"; return {}; }
    error.clear(); return info;
}
}
std::optional<SND_SE_INFO> ResolveRetailEffect(int id, int basic, int chapter, int voice, std::string &error) {
    if (id < 0 || id >= 2800) { error = "Chronicle sound effect IDs must be 0..2799"; return {}; }
    if (id >= 100 && id < 300 && basic >= 0) {
        if (basic > 1) { error = "invalid loaded basic sound table"; return {}; }
        return Entry(basic == 0 ? std::span<const SND_SE_INFO>(geo) : std::span<const SND_SE_INFO>(dun), id - 100, true, error);
    }
    if (id >= 300 && id < 400 && chapter >= 0) {
        if (chapter >= 101) { error = "invalid loaded chapter sound table"; return {}; }
        if (const auto *table = cap_se_info[chapter]) return Entry(Known(table), id - 300, true, error);
    }
    if (id >= 400 && id < 500 && voice >= 0) {
        if (voice >= 11) { error = "invalid loaded voice sound table"; return {}; }
        if (const auto *table = voice_info[voice]) return Entry(Known(table), id - 400, true, error);
    }
    return Entry(se_info, id, false, error);
}
}

namespace mods {
bool RetailEffectHasSamples(const audio::Bank *bank, int program, int effect) {
    if (!bank || program < 0 || program > 127 || effect < 0 || effect > 127) return false;
    std::array<audio::Layer, 8> layers{}; // Match the official mixer layer limit.
    int count = bank->Resolve(program, effect, 127, layers);
    if (!count) count = bank->ResolveSplit(program, effect, 127, layers);
    for (int i = 0; i < count; ++i)
        if (layers[i].vag && !layers[i].vag->pcm.empty() && layers[i].rate > 0) return true;
    return false;
}
}
