#include "town_motion.hpp"

#include <array>
#include <stdexcept>

#include "main.hpp"

extern s32 mode;

namespace mods {
namespace {
const TownMotionBinding *binding = nullptr;
constexpr std::array     motion_map{0, 1, 2, 0, 0, 34, 35, 31, 32, 0, 0};
} // namespace

TownMotionBinding::TownMotionBinding(CCharacter &character, int member) : character_(&character), member_(member) {
    if (member < 1 || member > 5) {
        throw std::invalid_argument("town motion binding requires a retail member from 1 to 5");
    }
    if (binding) {
        throw std::logic_error("a town motion binding is already active");
    }
    binding = this;
}

TownMotionBinding::~TownMotionBinding() {
    if (binding == this) {
        binding = nullptr;
    }
}

int TownMotionBinding::Resolve(const CCharacter *character, int motion) const {
    if (character != character_ || motion < 0 || motion >= static_cast<int>(motion_map.size())) {
        return motion;
    }
    // Goro's retail base.cfg marks slot 32 as NONE (0,0,0). Native
    // MotionProc cannot play that slot. Keep the idle pose for this optional
    // town action instead of entering an absent animation.
    return member_ == 2 && motion == 8 ? 0 : motion_map[motion];
}

int ResolveTownMotion(const CCharacter *character, int motion) {
    // No mod demand means no game-state inspection or mutation.
    if (!binding) {
        return motion;
    }
    return mode == GAME_MODE_EDIT ? binding->Resolve(character, motion) : motion;
}
} // namespace mods
