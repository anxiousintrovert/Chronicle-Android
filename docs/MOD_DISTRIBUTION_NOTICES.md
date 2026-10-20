# Windows candidate dependency and asset notice inventory

This inventory maps the documented Windows build's dependencies to the files
collected by `chronicle_windows_bundle.py`. It records provenance evidence, not
a new license grant. Keep each original notice intact. Runtime DLLs must match
the documented dependency files byte-for-byte before staging, so an old DLL in
the build directory cannot silently inherit another version's notices.

| Component | Collected bundle notice | Source used |
|---|---|---|
| Chronicle contributors | `notices/CREDITS` | Repository CREDITS; attribution, not a license |
| Kazarov host and retained port code | `notices/Kazarov.txt`, `notices/Kazarov-retail.txt` | Pinned submodule LICENSE and retained third-party notice |
| Lua 5.5.1 | `notices/Lua.txt` | Vendored LUA-LICENSE.txt |
| SDL 3.4.18 | `notices/SDL.txt` | Pinned Windows SDL dependency LICENSE.txt |
| llvm-mingw 20260922 runtimes | `notices/LLVM.txt`, `notices/mingw/*` | Toolchain LICENSE.TXT and x64 runtime/MinGW/winpthreads copying notices |
| nlohmann JSON 3.12.0 | `notices/JSON.txt` | Fetched source LICENSE.MIT |
| simdjson 4.6.11 | `notices/simdjson.txt`, `notices/simdjson-MIT.txt` | Both fetched source notices |
| fastgltf 0.9.1 | `notices/fastgltf.md` | Fetched source LICENSE.md |
| dr_wav, dr_flac, stb_vorbis, stb_truetype, stb_image | `notices/embedded/*.txt` | Complete pinned single-file sources, preserving their embedded notices |
| Regular/thin message fonts | `notices/Fonts.md` | Repository font README, including named authors and recorded permission history |
| Controller glyph atlases | `notices/Glyph-provenance.md` | Repository GLYPHS.md; supplied-art source/license remains unresolved |

The static library inputs were checked against the pinned Kazarov CMake host and
glTF definitions and Chronicle's Lua target. Build tools such as Ninja, glslang,
CMake and the compiler executables are not distributed. GoogleTest and the test
executable are also excluded. The Vulkan loader comes from the installed graphics
driver; no `vulkan-1.dll` is copied into the bundle.

## Runtime import check

For candidate 111f55ce, llvm-readobj inspected the imports of both executables and
all four distributed DLLs. The game's external non-system imports were SDL3,
libc++, libunwind and the system Vulkan loader. The remaining imports were Windows
system/UCRT libraries. `libwinpthread-1.dll` remains in the documented runtime
list, with its notice, even though these executable import tables did not name it.
This is static import evidence; it does not enumerate every dynamically selected
driver or optional SDL backend. The extracted candidate also passed startup with
development-tool directories removed from PATH.

## Unresolved supplied-art provenance

The glyph documentation explicitly records that no license or attribution file
was present with the supplied colored-button assets. Local history identifies
the initial atlas addition at 9ffdb15c and the colored PS5 style at 663dc72d,
[upstream PR #152](https://github.com/TheMoonPeople/Chronicle/pull/152). The PR body and comment list supplied no additional provenance;
the nine atlas PNGs contained no metadata identifying a source or license.

A source pack link or original license/attribution file is still needed. Do not
infer that the Kazarov or library licenses cover this artwork, or silently replace
the inherited default art to close this checklist. Font permission is recorded
separately in the font README. The local candidate remains unapproved while the
release checklist and this provenance question are open.
