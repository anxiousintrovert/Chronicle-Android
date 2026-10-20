#pragma once
#include <optional>
#include <string>
#include "snd.hpp"
#include "runtime.hpp"
namespace audio { class Bank; }
namespace mods {
// Read-only table lookup for explicitly requested mod effects. It does not
// imply the corresponding bank/sample is currently loaded or dispatch audio.
std::optional<SND_SE_INFO> ResolveRetailEffect(int id, int basic, int chapter, int voice, std::string &error);
nlohmann::json ReadRetailBgm();
bool StopRetailBgm(const std::vector<GameCommand> &commands, int money, std::string &error);
bool RetailEffectHasSamples(const audio::Bank *bank, int program, int effect);
// Checks the actual loaded port bank and resolves native default volume.
bool RetailEffectReady(const SND_SE_INFO &row, int volume, std::string &error);
// Explicit commit-only dispatch; uses the normal driver Step queue.
bool QueueRetailEffect(const SND_SE_INFO &row, int volume, std::string &error);
std::string ValidateRetailEffect(const nlohmann::json &arguments);
bool ApplyRetailEffect(const std::vector<GameCommand> &commands, int money, int basic, int chapter, int voice, std::string &error);
}
