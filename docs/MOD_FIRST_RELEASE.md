# Chronicle mod framework first release

The first release prioritizes quests, texture replacements and a reliable author/user workflow. Full MoonMod coverage is no longer a release requirement. Existing tested APIs can remain available, but broader APIs must not delay these priorities.

## Required scope

- Folder packages, strict manifests, new mods disabled by default, dependency/version checks and deterministic activation.
- Shared-framework mod manager with enable/disable, restart requirements, conflict reasons and useful diagnostics. No bundled enabled gameplay mods.
- Same-size compatible native asset replacements without modifying extracted originals.
- Full texture replacement: independently decoded replacement images with different resolutions/file sizes, preserving native logical coordinates and UV behavior. Use a dedicated texture path, not arbitrary larger reads into native asset buffers. Validate dimensions, memory limits, target identity and conflicts; retain original texture fallback. The package path, discovery/export tools, upload fallback and Windows retail 4K validation are implemented; broader palette/copy/platform scenarios remain unverified.
- Owned quests with named objectives, explicit start, progress, supported events, one-time transactional rewards, save/load and manager browsing. Document precisely which events/content can be authored; foreign quest formats and native story replacement are not implied.
- Author documentation, valid disabled examples, package creation/check/pack/install tooling and diagnostic reports.
- Regression evidence for empty/disabled packages, conflict fallback, restart/disable, quest persistence/rewards and representative retail texture rendering. Use Clang; leave every PS2 directory read-only; keep upstream Kazarov.

## Release boundaries

Defer broader MoonMod parity, registered maps/characters/animations, advanced save/NG+ services, remote discovery and bespoke character-sheet UI. Contributor-owned custom item, weapon and cutscene frameworks remain separate; do not allocate their registries or impose their implementation.

The first release must state the platforms actually validated. A Windows build does not certify Linux/macOS/Android. Existing broader experimental APIs are not evidence that all of their retail scenarios are release-certified.

## Next implementation priority

Audit the quest authoring workflow and examples against the implemented host contracts, including explicit start, named objectives, event delivery, one-time rewards and save/reload. Texture discovery, full-resolution replacement, conflict/disable cases and a real 4096x4096 GPU upload have Windows evidence below. Palette/copy behavior and other platforms need separate validation. The dated sections below record historical milestones; outstanding statements in older entries describe their state at that milestone.

## Full-resolution texture upload foundation (2026-10-20)

The port decoded-texture object can now carry optional logical dimensions independently of physical image dimensions. The registry retains logical texel dimensions and forwards both dimensions to the existing upstream Kazarov TextureDesc. Defaults are zero, preserving original behavior. This is internal groundwork, not an exposed package capability.

Clang 23.1.2 game/test builds pass. A GPU test uploads a 16x8 image addressed as 4x2, verifies full-resolution readback, native full-image and atlas-subrectangle rendering, and registry release. All 15 focused texture/localization checks pass. Full suite: 1,028 passed, 103 skipped and one disabled. PS2 baseline and Kazarov remain unchanged. Package manifest/loading, bounded PNG validation, original-content identity/discovery, conflict handling and retail replacement/disable scenarios remain outstanding.

## Replacement PNG validation groundwork (2026-10-20)

An opt-in PNG loader now checks regular-file status, encoded length and signature, header dimensions and decoded pixel count before image decoding. Defaults are 32 MiB encoded, 8192 pixels per dimension and 16 million output pixels. Failed reads/decodes leave caller outputs unchanged. These bound encoded input and RGBA output, not all transient memory inside the shared decoder; this is not a hardened hostile-file sandbox. Existing UI/localization calls retain their original path.

Clang 23.1.2 game/test builds and all 17 focused PNG, texture and localization tests pass, including exact limit boundaries, huge declared dimensions, truncated data, signature rejection and unchanged outputs. PS2 and Kazarov remain unchanged. This utility is not yet connected to package activation; full texture replacement remains outstanding.

## Full-resolution package integration (2026-10-20)

`assets.textures.v1` now accepts name plus original decoded-content SHA-256 targets and bounded PNG sources. The active manager snapshot owns replacements; duplicate targets conflict, dependencies propagate blocking, and newly discovered packs remain disabled. Native texture entry applies matched replacements after port localization while retaining logical dimensions. Invalid/changed sources fall back before upload. Offline check/pack/install/check-set understands declarations and conflicts; runtime additionally decodes PNG data. See MOD_TEXTURES.md for exact contracts and limitations.

Clang 23.1.2 game/test builds passed. The full suite recorded 1,031 passes, 105 skips and one disabled test. All 70 Python checks passed. New native tests cover disabled/conflicting targets, content mismatch, invalid-source fallback, actual texture entry/GPU color regions, retained full pixel dimensions and disable/restart. PS2 and Kazarov remain unchanged. Author-friendly target discovery, retail capture/replacement, palette/copy scenarios and GPU allocation fallback are still release work.

## Replacement upload fallback (2026-10-20)

The port now keeps original decoded pixels/palette intact until a replacement upload succeeds. A failed replacement upload releases returned temporary registry resources and retries the original. The no-target path calls the original uploader once without copying original pixels. Clang 23.1.2 game/test builds and all 22 focused texture/package/PNG/localization tests pass, including an injected upload failure with palette cleanup and GPU verification of the original red image. PS2 and Kazarov are unchanged. Real device-loss/out-of-memory recovery is not certified; target discovery and retail replacement validation remain next.

## Texture discovery/export tooling (2026-10-20)

`--discover-textures DIR` now captures original pre-localization/pre-mod RGBA into atomic, content-identified snapshots in a new directory. Capture is dormant by default, rejects PS2 paths, deduplicates targets and caps output at 2048 textures/256 MiB. `tools/chronicle_texture.py inspect/export` validates captures and writes lossless RGBA PNGs plus ready-to-copy target declarations without third-party Python dependencies.

Clang 23.1.2 game/test builds passed. Full suite: 1,036 passed, 105 skipped, one disabled. All 71 Python checks passed. The local PAL dungeon-entry run produced 45 snapshots with zero discovery errors; the dungeon-entry snapshot exported successfully with its matching identity. Native and Python tests cover dormant capture, rejected output paths, indexed alpha expansion, deduplication, malformed snapshots, hash mismatch and overwrite refusal. PS2 and upstream Kazarov are unchanged. Retail replacement rendering, not just capture/export, remains outstanding.

## Retail full-resolution and 4K validation (2026-10-20)

Added a repeatable isolated PAL texture smoke runner and native gameplay fixture. Baseline, 512x512 diagnostic body replacement, 4096x4096 upscaled original, disabled and conflict cases all passed. Native logical dimensions stayed 256x256; GPU physical dimensions matched 512/4096 only when enabled. The full 4K GPU readback hash matched all 64 MiB of generated source RGBA. Screenshots verified visible diagnostic replacement and normal appearance of the upscaled original. No gameplay-script modules were active. Disabled/conflicting packs restored native pixels in new sessions.

Clang 23.1.2 test builds and all 71 Python tests passed. PS2 and Kazarov remain unchanged. Retail evidence remains local. Broader palette/copy/platform scenarios and first-release documentation/quest workflow review remain to be audited; this is not a claim of complete framework or vanilla playthrough certification.

## Runnable quest author example (2026-10-20)

Dungeon Contract is a separate, default-disabled example using the shared action
and quest pages. Its integration test loads the exact shipped manifest/Lua and
verifies disabled startup, deferred explicit start, named floor/death objectives,
scripted-kill filtering, partial save restoration, a native 50 Gilda commit and
completed-save restoration without replaying rewards. Repeating Start after
completion is harmless. No custom item/weapon/cutscene registry is introduced.

Clang 23.1.2 test build, all 25 focused quest/menu tests, all 71 Python tests and
offline package validation passed. PS2 and upstream Kazarov are unchanged. This
is runtime integration evidence, not a retail playthrough of the authored quest.
