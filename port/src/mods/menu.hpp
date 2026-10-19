#pragma once
#include <cstdint>

namespace mods::menu {
void Open();
void Close();
bool IsOpen();
void Run(std::uint16_t pressed);
void Draw();
} // namespace mods::menu
