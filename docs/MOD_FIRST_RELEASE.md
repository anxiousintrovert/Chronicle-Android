# Chronicle mod framework first release

The first release prioritizes quests, texture replacements and a reliable author/user workflow. The broader roadmap is not a first-release requirement. Existing tested APIs can remain available, but broader APIs must not delay these priorities.

## Required scope

- Folder packages, strict manifests, new mods disabled by default, dependency/version checks and deterministic activation.
- Shared-framework mod manager with enable/disable, restart requirements, conflict reasons and useful diagnostics. No bundled enabled gameplay mods.
- Same-size compatible native asset replacements without modifying extracted originals.
- Full texture replacement: independently decoded replacement images with different resolutions/file sizes, preserving native logical coordinates and UV behavior. Use a dedicated texture path, not arbitrary larger reads into native asset buffers. Validate dimensions, memory limits, target identity and conflicts; retain original texture fallback. The package path, discovery/export tools, upload fallback and Windows retail 4K validation are implemented; broader palette/copy/platform scenarios remain unverified.
- Owned quests with named objectives, explicit start, progress, supported events, one-time transactional rewards, save/load and manager browsing. Document precisely which events/content can be authored; foreign quest formats and native story replacement are not implied.
- Author documentation, valid disabled examples, package creation/check/pack/install tooling and diagnostic reports.
- Regression evidence for empty/disabled packages, conflict fallback, restart/disable, quest persistence/rewards and representative retail texture rendering. Use Clang; leave every PS2 directory read-only; keep upstream Kazarov.

## Release boundaries

Defer broader API expansion, registered maps/characters/animations, advanced save/NG+ services, remote discovery and bespoke character-sheet UI. Contributor-owned custom item, weapon and cutscene frameworks remain separate; do not allocate their registries or impose their implementation.

The first release must state the platforms actually validated. A Windows build does not certify Linux/macOS/Android. Existing broader experimental APIs are not evidence that all of their retail scenarios are release-certified.

## Testing beta acceptance gates

The current delivery target is a Windows testing beta, not completion of the
broader capability roadmap. Actor lifecycle subscriptions and other new domains
are deferred. Existing internal actor queue groundwork is not a public API.

| Gate | Evidence required before beta delivery | Current boundary |
|---|---|---|
| Quest playthrough | Start the shipped contract through the shared menu, enter a floor, defeat enemies through normal controls, receive exactly one reward, save/reload, and verify cancellation/restart | Scripted controller start, native menu save/load and abandon/restart passed for an unadvanced contract; normal-controls floor/combat/completed-reward acceptance remains pending |
| Clean installation | Launch from a new distribution/save directory; install disabled packages, enable/restart, diagnose conflicts, disable/restart and recover original textures | Staged Windows PNG-package rehearsal passed through scripted controller input in the shared manager; conflict/disabled scenes matched the no-mod baseline |
| Texture acceptance | Same-size replacement, full-resolution replacement and 4096x4096 source upload; disabled/conflicting fallback | Windows PAL evidence exists; rerun against the beta candidate and record its exact commit |
| Regression | Clang game/test builds, focused/full native tests, author-tool tests, PS2 baseline and unchanged upstream renderer pin | Prior runs exist; record fresh results for the candidate, including skips |
| Distribution | Reproducible Windows Clang build, required runtime DLLs, license notices, checksums and source commit | Clean-source candidate 111f55ce archived and extracted startup passed; dependency notices and hashes included. Notice review and release approval remain pending |
| Tester guide | Setup, backup/test-save procedure, package format, enable/restart/conflict workflow, known limitations and bug report evidence | [Tester procedure](MOD_BETA_TESTING.md) documents setup and acceptance; candidate-specific results and limitations remain pending |

Do not label a candidate release-ready solely because unit tests pass. Record
normal-input, runtime-injected and source-only evidence distinctly. Windows beta
validation does not certify Android, Linux or macOS. Broader palette/copy cases
and experimental APIs must be identified as unverified where applicable.

## Clean Windows Clang build and staged startup (2026-10-20)

The Windows configure helper now explicitly selects llvm-mingw Clang for both
C and C++, including the Lua library. A new RelWithDebInfo build directory
configured with Clang 23.1.2 and built `darkcloud` and `dcdata` successfully.
The source was e28c111b plus this compiler-selection change; upstream Kazarov
remained 04bd301efd8486c033caf46faf52f179a6022d7c.

`cmake --install` into a new directory supplied both executables, `lang` and
`glyphs`. Copying the four documented runtime DLLs completed the startup inputs.
With PATH restricted to Windows/System32 and Windows, and DC_* environment
overrides cleared, the staged executable completed a three-frame offscreen PAL
startup on an RTX 3080 and exited zero. Its fresh save's mod report contained
empty errors, load_order and mods arrays. The executable SHA-256 was
`5dfb287aaacf9314ca476ceb900ae6cf29d271a86663ee7d2e8e21a8e4cdb0b2`.

This establishes clean compilation and initial runtime dependency resolution,
not a full gameplay or distribution acceptance run. License-notice collection,
archive/checksum automation, normal-controls quest playthrough and the staged
enable/conflict/disable/restart workflow remain pending. The staging directory
also holds local test saves/evidence and must not itself be distributed.

## Allowlisted bundle and refreshed regression (2026-10-20)

`tools/chronicle_windows_bundle.py` stages the six runtime executable/DLL files,
source language/font and referenced glyph assets, mod documentation, and pinned
dependency notices. It does not recursively copy the build directory. A new
output directory is required, PS2 output is rejected, and both recorded compilers
must be Clang. The build cache must identify the current source checkout.
`bundle.json` records file sizes/hashes, compiler records and checkout state;
it explicitly does not certify binary provenance or release readiness.

The local run staged 61 files and verified every recorded hash, without any mod,
save, data or capture paths. A launch from that generated bundle with a fresh
external save directory, PAL data and PATH restricted to Windows directories
completed three offscreen frames and exited zero. No test evidence was written
inside the bundle. Dependency notices include Lua, Kazarov, SDL, LLVM/MinGW,
JSON, simdjson, fastgltf, embedded decoder/font libraries and asset provenance.
The existing glyph provenance document records missing supplied-art attribution;
notice review remains an explicit distribution task. No release archive was
published by this helper.

The native source at 0368ceb0 was rebuilt with llvm-mingw Clang 23.1.2. Full
regression: 1,064 passed, 107 skipped and one disabled. The isolated PAL texture
runner passed baseline, unchanged PNG, diagnostic replacement, 4096x4096 upscale,
disabled and conflicting cases against the rebuilt test binary. All 85 Python
tests passed after adding the bundle checks. PS2 baseline and upstream Kazarov
remain unchanged. Normal-controls quest acceptance and the complete staged
install/enable/conflict/disable/restart workflow are still outstanding.

## Staged install and shared-menu restart rehearsal (2026-10-20)

The new `chronicle_beta_install_smoke.py` ran the staged Windows executable from
the previous clean Clang build, with a fresh external save directory and a Windows
system-only PATH. It verified the bundle's hashes, packed/installed two generated
texture packages using the author CLI, and used the game's scripted controller
input to open title Options > Mods > Mod Manager. No enable profile was written
by the runner. Fourteen phases passed: baseline, disabled installation, enable
preview, enabled restart/scene, second disabled installation, conflict preview,
blocked restart/scene, conflict disable preview, recovered restart, original
disable preview, disabled restart/scene.

Screenshots confirmed the current-session/next-launch distinction. The conflict
screen named the texture and competing owner; both packages were blocked after
restart. The enabled town scene visibly applied the magenta diagnostic to the
intended body texture. Full-image RGB comparisons found the conflict and disabled
scenes pixel-identical to the no-mod baseline; the enabled image differed only
within the character's replacement region. Both final profile selections were
false. Startup reports stayed unchanged during preview edits as designed.

The fixture uses startup jumps and scripted normal pad input, not direct calls to
the manager or runtime toggle functions. This closes the representative staged
texture installation/restart gate; it does not close the normal-controls quest
playthrough gate or certify every asset/platform. No native engine, PS2 or Kazarov
sources changed. Final notice review and release archive remain pending.

## Verified local candidate archive (2026-10-20)

`chronicle_bundle_archive.py` now verifies the staged manifest and every listed
file, rejects unlisted files or linked inputs, writes a sorted ZIP with fixed
metadata, reads back every entry, and writes a SHA-256 sidecar. It refuses existing
outputs, output inside the bundle, PS2 paths and unsafe/duplicate manifest paths.
Unit tests cover repeatability, exact readback, changed binaries, unexpected saves
and path rejection. All 88 Python checks passed.

The 61-file staged bundle plus its manifest produced a verified 62-entry local
archive, SHA-256 `adf7758d96807bd8ed84a2f788fd82e15abfe4c89c3aa2fe73e8afe9f50d866e`.
This is an archive-tool rehearsal using the earlier staged build, not the approved
release candidate. It contains no test save or retail capture. Final clean-source
staging, notice review and normal-controls quest acceptance remain open; archive
creation alone must not be treated as approval to publish.

## Controller-driven quest start/save/load and cancellation (2026-10-20)

The staged Windows game installed and enabled the exact shipped Dungeon Contract
1.1.0, started a normal new game from the title, and reached Norune through its
opening. Scripted pad input navigated town Options > Mods > Actions to start the
contract, then the native Save/Load menu saved slot 1. The saved trailer checksum
verified, required version 1.1.0 and contained the owned started flag.

Separate game processes loaded that slot through the title Load option. Reviewed
quest-page screenshots showed In progress 0/3 after load, Not started 0/3 after
Abandon, and In progress 0/3 after Start again. The input sequences are preserved
in [the quest menu fixtures](../tools/fixtures/beta-quest-menu/README.md); retail
screenshots and saves remain local. This uses the existing staged Clang binary;
no engine, PS2 or renderer source changed.

The contract had no completed objectives. Dungeon travel from this early save
remains locked by the normal mayor introduction. A separate debug-dungeon melee
probe ended against a wall and did not establish a quest kill. Neither result
closes floor/combat/reward acceptance. No story flags, HP values or native events
were changed to manufacture a pass.

## Clean-source Windows candidate 111f55ce (2026-10-20)

The documented Windows helper reran the RelWithDebInfo build with llvm-mingw
Clang 23.1.2 before staging the clean checkout
`111f55ce8bafda9cf2efcff5481d27a3e51b30c0`. The generated manifest records
`checkout_dirty: false` and upstream Kazarov
`04bd301efd8486c033caf46faf52f179a6022d7c`.

- Local archive: `chronicle-mod-framework-111f55ce-windows-x64.zip`
- ZIP SHA-256: `68aa0a1f0e8ef08773a5877a4b1fd3a0dbb7cdf078a3011406e548615ec86ff7`
- Executable SHA-256: `5dfb287aaacf9314ca476ceb900ae6cf29d271a86663ee7d2e8e21a8e4cdb0b2`
- Contents: 61 selected files plus the manifest, with no preinstalled mods,
  saves, extracted game assets or local test captures.

The archive passed full entry readback, was extracted into a new directory, and
completed a three-frame offscreen PAL startup using a new external save directory
and PATH restricted to Windows directories. The binary hash matches the earlier
staged installation/quest-menu evidence. This candidate is local and has not been
published as an approved release. Quest floor/combat/completed-reward acceptance
and notice review remain open. PS2 baseline and renderer pin checks passed.

## Dependency notice and runtime identity audit (2026-10-20)

The [notice inventory](MOD_DISTRIBUTION_NOTICES.md) now maps collected notices to
the pinned host/glTF/Lua build inputs, DLLs and source assets. llvm-readobj inspected
both executables and all four distributed DLLs; their ordinary imports matched
the documented runtimes, Windows system/UCRT libraries and system Vulkan loader.
Every distributed DLL matched its pinned dependency file byte-for-byte.

Staging now enforces that DLL identity check before copying any files, preventing
stale runtime binaries from being packaged with a different version's notices.
All 89 Python tests passed, including rejection of each replaced runtime DLL.
Native sources, PS2 files and Kazarov remain unchanged.

The glyph source/license remains unresolved after checking repository history,
the upstream colored-glyph PR and atlas metadata. A source pack link or original
attribution/license file has been requested. Notice collection and code-dependency
inventory are complete for this candidate; this outstanding asset provenance and
the quest floor/combat/reward acceptance still prevent release approval.

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

## Quest example shared-menu integration (2026-10-20)

The shipped Dungeon Contract now also has a shared-menu integration test. It
opens the real action page and presses Cross, verifies that starting is deferred
until a gameplay tick, renders registered/active/completed quest pages and the
reward receipt, and checks that browsing/filtering causes no state or money
writes. The optional PAL-data run loads retail font assets for screenshot review;
the action and completed reward pages were visually inspected. Floor/death events
are injected here: actual combat-to-quest delivery remains a separate retail gate.
Clang 23.1.2 build and the PAL-backed menu test passed.

## Native death-to-quest retail integration (2026-10-20)

A PAL-backed test loads two real dungeon enemies and prepares lethal poison ticks
in test-only code. Native CheckDmg emits two non-scripted death records; the normal
game tick drains the queue, completes the event objective and commits 50 Gilda.
The test verifies deferred delivery, the saved reward receipt, unchanged money
after further frames, and death capture turning off after completion. It neither
pushes event records nor calls the scripted damage API. The run passed at frame
6533, with money changing from 0 to 50.

This validates native poison damage through quest rewards with retail resources.
It is a controlled native-path probe, not a controller-driven melee playthrough,
and does not complete the separate floor-transition or disk save/reload gates.
Run the test alone with DC_DATA and a new DC_QUEST_RETAIL_AUDIT directory. Clang
23.1.2 was used; no production engine, PS2 or Kazarov files changed.

## Quest disk save/load integration (2026-10-20)

The shipped Dungeon Contract is now exercised through CMemoryCardAccess manual
save/load and SaveSlotWriteAutosave. Completed native money and the owned reward
receipt restore together without regranting. Loading the earlier partial save
restores its zero reward money and unfinished objective, which can then complete
normally. A corrupt mod trailer rejects loading while preserving both live native
money and owned quest state. The manual test refreshes SetBuff as opening the
save menu does; reusing an old menu snapshot would test a stale native image.

Clang 23.1.2 test build and all 40 focused save/quest/menu tests passed. Events
are injected in this disk test; the separate retail native-poison probe validates
the producer/queue path. These results do not certify a controller-driven full
quest playthrough or other platforms. No production engine, PS2 or Kazarov edits.

## Quest author-tool starter (2026-10-20)

`chronicle_mod.py new --template quest` generates the tested Dungeon Contract
workflow under the author's chosen ID, retaining default-disabled installation.
The basic template remains the default. Tests cover CLI generation, exact Lua
content reuse, manifest validation, pack/install preservation, no enable-profile
creation, existing-directory rejection and PS2-path rejection before creation.
All 73 Python tests passed. No native sources or compiler selection changed.

## Opt-in objective ordering (2026-10-20)

quests.ordering.v1 allows named objectives to depend on completion of earlier
objectives via after. Existing quests without prerequisites retain their behavior.
The shared quest page names unmet prerequisites; events and scripted advances
honor the same gates, and save loading rejects impossible progress. This is
objective sequencing, not a cutscene or branching-dialogue framework. See the
host API and parity ledger for contracts and regression evidence.

## Explicit quest failure/recovery (2026-10-20)

quests.failure.v1 adds author-requested failure with a saved reason and explicit
resume retaining progress. Failed dynamic quests stop progression and death-event
demand. Completed quests cannot fail or resume. The shared quest page adds a
Failed filter and reason details. No automatic native failure policy is enabled.
Clang game/test builds, full regression and PAL menu review passed; see the host
API and coverage ledger for contracts, counts and platform limits.

## Texture package author command (2026-10-20)

chronicle_texture.py package builds a disabled texture-only package from a
verified discovery snapshot and a replacement PNG. It retains the original target
identity and copies PNG bytes unchanged, with logical/pixel sizes in its README.
PNG header limits are shared with the offline package validator; runtime decoding
remains mandatory. Tests cover generated manifests, exact image preservation,
pack/install, no enable profile, existing-output refusal and rejected PS2/invalid
inputs before output creation. All 74 Python tests passed.

The retail smoke runner now creates packages through this helper. All six PAL
cases passed: native baseline, unchanged PNG, diagnostic replacement, full 4K
upscale, disabled and conflict. The 4K GPU pixel hash still matched the complete
source. The existing Clang-built test binary was used. No native engine, PS2 or
Kazarov sources changed in this tooling update.
