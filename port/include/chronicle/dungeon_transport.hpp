#pragma once
#include <span>
#include <vector>

// Internal adapter for the verified PAL built-in transport programs. An empty
// result means the original bytecode can be used. Never edits the input buffer.
std::vector<char> ExpandedTransportScript(std::span<const char> script, int dungeon,
                                          int front_extent, int back_extent);

// Preflight/cache the currently loaded native program before publishing maps.
void PrepareLoadedExpandedTransport(int dungeon);
