# Chronicle mod framework first release

The first release prioritizes quests, texture replacements and a reliable author/user workflow. Full MoonMod coverage is no longer a release requirement. Existing tested APIs can remain available, but broader APIs must not delay these priorities.

## Required scope

- Folder packages, strict manifests, new mods disabled by default, dependency/version checks and deterministic activation.
- Shared-framework mod manager with enable/disable, restart requirements, conflict reasons and useful diagnostics. No bundled enabled gameplay mods.
- Same-size compatible native asset replacements without modifying extracted originals.
- Full texture replacement: independently decoded replacement images with different resolutions/file sizes, preserving native logical coordinates and UV behavior. Use a dedicated texture path, not arbitrary larger reads into native asset buffers. Validate dimensions, memory limits, target identity and conflicts; retain original texture fallback. This remains outstanding.
- Owned quests with named objectives, explicit start, progress, supported events, one-time transactional rewards, save/load and manager browsing. Document precisely which events/content can be authored; foreign quest formats and native story replacement are not implied.
- Author documentation, valid disabled examples, package creation/check/pack/install tooling and diagnostic reports.
- Regression evidence for empty/disabled packages, conflict fallback, restart/disable, quest persistence/rewards and representative retail texture rendering. Use Clang; leave every PS2 directory read-only; keep upstream Kazarov.

## Release boundaries

Defer broader MoonMod parity, registered maps/characters/animations, advanced save/NG+ services, remote discovery and bespoke character-sheet UI. Contributor-owned custom item, weapon and cutscene frameworks remain separate; do not allocate their registries or impose their implementation.

The first release must state the platforms actually validated. A Windows build does not certify Linux/macOS/Android. Existing broader experimental APIs are not evidence that all of their retail scenarios are release-certified.

## Next implementation priority

Inspect port texture decode/upload and upstream renderer logical-size handling. Add package-owned full-resolution texture declarations and target discovery that can distinguish textures reliably, then exercise conflicting packs, original fallback, atlas/UV behavior and disable/restart on real assets. Do not silently downscale a purported high-resolution replacement to satisfy old buffer sizes.

## Full-resolution texture upload foundation (2026-10-20)

The port decoded-texture object can now carry optional logical dimensions independently of physical image dimensions. The registry retains logical texel dimensions and forwards both dimensions to the existing upstream Kazarov TextureDesc. Defaults are zero, preserving original behavior. This is internal groundwork, not an exposed package capability.

Clang 23.1.2 game/test builds pass. A GPU test uploads a 16x8 image addressed as 4x2, verifies full-resolution readback, native full-image and atlas-subrectangle rendering, and registry release. All 15 focused texture/localization checks pass. Full suite: 1,028 passed, 103 skipped and one disabled. PS2 baseline and Kazarov remain unchanged. Package manifest/loading, bounded PNG validation, original-content identity/discovery, conflict handling and retail replacement/disable scenarios remain outstanding.

## Replacement PNG validation groundwork (2026-10-20)

An opt-in PNG loader now checks regular-file status, encoded length and signature, header dimensions and decoded pixel count before image decoding. Defaults are 32 MiB encoded, 8192 pixels per dimension and 16 million output pixels. Failed reads/decodes leave caller outputs unchanged. These bound encoded input and RGBA output, not all transient memory inside the shared decoder; this is not a hardened hostile-file sandbox. Existing UI/localization calls retain their original path.

Clang 23.1.2 game/test builds and all 17 focused PNG, texture and localization tests pass, including exact limit boundaries, huge declared dimensions, truncated data, signature rejection and unchanged outputs. PS2 and Kazarov remain unchanged. This utility is not yet connected to package activation; full texture replacement remains outstanding.
