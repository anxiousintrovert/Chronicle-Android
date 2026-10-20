#include "player_tuning_native.hpp"

#include <algorithm>
#include <cmath>

#include "btactstatus.hpp"
#include "character.hpp"
#include "dun/gameloop.hpp"
#include "main.hpp"
#include "userstatus.hpp"

extern s32 mode;

namespace mods {
namespace {
PlayerTuning tuning;

bool Active() { return tuning.Engaged() && mode == GAME_MODE_DUNGEON; }
} // namespace

PlayerTuning &OwnedPlayerTuning() { return tuning; }

void RetainPlayerTuningOwners() {
    if (tuning.Engaged()) {
        tuning.Retain(Scripts().ActiveOwners(), Scripts().Epoch());
    }
}

void ScalePlayerValue(PlayerTuning::Field field, float &value) {
    if (!Active()) {
        return;
    }
    const float scale = static_cast<float>(tuning.Value(field));
    if (scale != 1.0f) {
        value *= scale;
    }
}

void TunePlayerHitFrames(int &recoil, int &invincible) {
    if (!Active()) {
        return;
    }
    const float scale = static_cast<float>(tuning.Value(PlayerTuning::Recoil));
    if (scale != 1.0f && recoil > 0) {
        recoil = std::max(1, static_cast<int>(std::lround(recoil * scale)));
    }
    const int bonus = static_cast<int>(tuning.Value(PlayerTuning::InvincibleBonus));
    if (bonus) {
        invincible = std::max(invincible, recoil + bonus);
    }
}

void ApplyPlayerMapReveal(int &map, int &monsters) {
    if (!Active()) {
        return;
    }
    if (tuning.Value(PlayerTuning::MapReveal)) {
        map = 1;
    }
    if (tuning.Value(PlayerTuning::MonsterReveal)) {
        monsters = 1;
    }
}

void StepPlayerMotion() {
    if (!Active() || tuning.Value(PlayerTuning::AttackSpeed) == 1 || !UserStatus ||
        UserStatus->cur_chara != CHARA_TOAN || BtActStatus.action_on != 1 ||
        BtActStatus.action_no < 0x24 || BtActStatus.action_no > 0x28 ||
        BtActStatus.motion_no != BtActStatus.action_no || !CharaMain.motion_type.motion_info) {
        CharaMain.Step();
        return;
    }
    constexpr float last_window[5] = {828, 835, 844, 854, 868};
    const float     time = CharaMain.motion_type.state.time;
    const float     end = static_cast<float>(CharaMain.motion_type.motion_info[BtActStatus.motion_no].end);
    const float     tail = last_window[BtActStatus.action_no - 0x24];
    if (!(time > tail && time < end - 2)) {
        CharaMain.Step();
        return;
    }
    const float old = CharaMain.motion_speed;
    const float base = old > 0 ? old : CharaMain.motion_type.motion_info[BtActStatus.motion_no].speed;
    const float speed = std::min(end - 1.5f - time, base * static_cast<float>(tuning.Value(PlayerTuning::AttackSpeed)));
    CharaMain.motion_speed = speed;
    CharaMain.Step();
    // The override lasts only for this step; disabling cannot leave a boosted motion behind.
    if (CharaMain.motion_speed == speed) {
        CharaMain.motion_speed = old;
    }
}
} // namespace mods
