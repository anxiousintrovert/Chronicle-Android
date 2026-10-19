#pragma once
#include <cstddef>
#include <filesystem>
#include <string>

#include "platform/files.hpp"

namespace mods {
FilesResult WriteSave(const std::filesystem::path &path, const void *native, std::size_t size, bool replace);
bool        LoadSaveState(const std::filesystem::path &path, std::size_t native_size, std::string &error);
} // namespace mods
