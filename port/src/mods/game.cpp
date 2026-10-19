#include <algorithm>

#include "dngstatusdata.hpp"
#include "gamemode.hpp"
#include "main.hpp"
#include "runtime.hpp"
#include "savedata.hpp"
extern s32 mode;

namespace mods {
void TickGame() {
    const bool playing = SaveData && (mode == GAME_MODE_EDIT || mode == GAME_MODE_DUNGEON);
    World      world{mode, MapNo, playing ? SaveData->GetDngStatus()->money : 0, playing};
    Scripts().Tick(world, [playing](int amount) {
        if (!playing || !SaveData) {
            return false;
        }
        auto *status = SaveData->GetDngStatus();
        status->money = std::clamp(static_cast<int>(status->money) + amount, 0, 65535);
        return true;
    });
}
} // namespace mods
