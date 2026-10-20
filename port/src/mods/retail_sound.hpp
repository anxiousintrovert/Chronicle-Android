#pragma once
#include <optional>
#include <string>
#include "snd.hpp"
namespace mods {
// Read-only table lookup for explicitly requested mod effects. It does not
// imply the corresponding bank/sample is currently loaded or dispatch audio.
std::optional<SND_SE_INFO> ResolveRetailEffect(int id, int basic, int chapter, int voice, std::string &error);
}
