#pragma once
#include <cstdint>

namespace chronicle {
// Internal synchronous staging support; neither accessor advances the generator.
std::uint32_t RandomState();
void          RestoreRandomState(std::uint32_t state);
} // namespace chronicle
