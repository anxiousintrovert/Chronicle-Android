#pragma once

class CCharacter;

namespace mods {
// Internal lifetime token for a loaded retail town replacement. The loader must
// destroy this before replacing/retiring the character or leaving its scene.
// Construction does not load assets or change native character/save fields.
class TownMotionBinding {
public:
    TownMotionBinding(CCharacter &character, int member);
    ~TownMotionBinding();
    TownMotionBinding(const TownMotionBinding &) = delete;
    TownMotionBinding &operator=(const TownMotionBinding &) = delete;

    int Resolve(const CCharacter *character, int motion) const;

private:
    const CCharacter *character_;
    int               member_;
};

int ResolveTownMotion(const CCharacter *character, int motion);
} // namespace mods
