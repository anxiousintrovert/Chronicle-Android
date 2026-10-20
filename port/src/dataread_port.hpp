#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <string_view>

#include "common.h"

// The languages the extracted disc carries, as LanguageCode numbers, from the languages.json dcdata
// writes for its release (dcdata::ReleaseOf); none when the data has no such file.
std::vector<s32> DataLanguages();

// The languages the game can offer: the disc's own. A localization pack, once the port has them,
// adds the languages it carries here.
std::vector<s32> SupportedLanguages();

// Read-only preflight against the already initialized data index and active replacements.
bool DataFileAvailable(std::string_view path);

// Explicit mod preflight: bounded owned bytes, no shared buffers or localization.
// Requires an initialized data index and preserves out on failure.
bool ReadDataFileOwned(std::string_view path, std::size_t limit, std::vector<std::uint8_t> &out, std::string &error);
