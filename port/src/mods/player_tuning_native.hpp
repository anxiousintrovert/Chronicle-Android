#pragma once

#include "player_tuning.hpp"

namespace mods {
PlayerTuning &OwnedPlayerTuning();
void          RetainPlayerTuningOwners();
void          ScalePlayerValue(PlayerTuning::Field field, float &value);
void          TunePlayerHitFrames(int &recoil, int &invincible);
void          ApplyPlayerMapReveal(int &map, int &monsters);
void          StepPlayerMotion();
} // namespace mods
