# Chronicle framework capability coverage

Target: comparable Chronicle modding capabilities, using our own package/API/ABI.
Unchanged MoonMod package compatibility is not a requirement. Reference inspected:
MoonMod `6f5ca66e44ff29f866453b1181c9679aa9b6bd5d`, its API catalog, native SDK
documentation and Chronicle integration. Catalog names alone do not prove that
the reference has a working native implementation of every operation.

**Full capability parity has not been reached.** The initial expansion supplies
a real host-call boundary, 41 implemented operations, native plugins and author
tools. It does not expose unimplemented engine hooks as successful no-ops.

| Area | Current Chronicle framework | Remaining coverage |
|---|---|---|
| Packages and conflicts | Folder discovery, version-constrained dependencies, cycles, duplicate IDs, declared conflicts, exclusive claims and asset collisions; validated offline ZIP install/update with backups; new packages disabled | Automatic remote downloads/update discovery are not implemented |
| In-game management | Shared menu framework, enable/disable, restart preview, reasons, quest progress, errors/logs, permission display, native trust confirmation | Author-provided menu/action contributions, richer journal/skills UI |
| Lua and host calls | Isolated bounded Lua, simulation/map callbacks, owned save state/resources, operation discovery, permission and required-operation checks, queued atomic writes | Owned event subscriptions and more native lifecycle events; dependency exports/commands |
| Player / party | Joined/active stats and status duration reads; health, thirst, capacities and defense writes | Recruitment, native active/town-character switching, status application/removal, temporary tuning ownership |
| Inventory / weapons / economy | Native definition/bag/quick-slot/attachment reads; bounded retail item grant/removal and weapon grants; ABS/synthesis points and WHP writes; wallet/fishing transactions | Custom item/weapon registration belongs to contributor; attachment inventory give/remove and item use; weapon equip, build-up, attachment insertion/removal and status-break synthesis |
| World / dungeon | World/floor/restriction and dungeon progress snapshots; story/map/init flag transactions; vanilla generation unchanged | transitions, owned layout/population/loot/floor-rule overrides, back-floor requests, extra floors |
| Entities / combat | No new hooks or engine-capacity changes | Generation-checked handles, spawn/despawn, owned stats/scale, deterministic damage modifiers and kill events |
| Shops / Georama | Saved plot/placement/request inspection | Owned shop stock/prices with restore, placement/Atla/satisfaction integration |
| Content / assets | Same-size compatible existing asset replacements and package resource reads | Contributor-owned custom item/weapon registration; registered characters/models/animations/maps, structural validation/conversion, native allocation contracts |
| Quests / dialogue / skills | Declared owned objectives, scripted progress/rewards, persisted scalar state | Dialogue trees, native quest events, skill trees, native script interception |
| Rendering / audio | No renderer/timing modifications | Owned overlays/camera/lighting/audio controls, activated only by callers |
| Native plugins | Own C ABI 1, SHA-256 approval binding, separate trust+enable, Windows real DLL lifecycle test; shared host calls/logging/ticks | Native events, persistence/exports, broader host domains; Linux/macOS/Android verification |
| Author tooling | new/check/inspect/check-set/pack/install/capabilities/apis/compare-seeds CLI; standalone Clang native example | Engine content editors/converters, rich runtime inspection, automated retail smoke scenarios |
| Saves | Existing namespaced mod trailer, atomic file writes and exact package/version checks | Schema migrations, independent save branching, full NG+ transfer/recruitment/reset services |

Chronicle Two-specific systems (photography, inventions, Ridepod, monster badges,
Spheda) are outside this Chronicle-only framework's scope.

## Default-behavior gate

- No gameplay examples are copied into a runtime mods folder by the build.
- Manifest `enabled` cannot activate a package; local enable selection is required.
- No gameplay module means no game queries, writes or native plugin load.
- Reads are lazy detached snapshots. Writes are bounded and committed only after
  the calling callback succeeds. Disabling a package takes effect after restart.
- No generator array expansion, extra enemy scans, dungeon patch, RNG hook,
  combat formula, renderer replacement or timing change is part of this expansion.
- Native changes stay in `port/`; tools/docs/examples stay outside `ps2/`. Native
  signatures and layouts remain unchanged. Clang is required for C and C++.

The cross-build seed audit compares our unmodified pinned upstream
`50eaff633e4e02e5dbc6a7a6a84c7e34688cb738` against the framework with an empty
mod set and installed-but-disabled Lua/native packages. All 3,840 JSONL snapshots
match byte for byte (SHA-256
`8bdcc22d99722f06fb3bc9006836270a7f965a3ac09e895d51e41ad5c3b6a877`).
This includes room rectangles/counts, all nonempty cells, links, linked-room key
chests, gate key, map seed, next RNG, native grid and map object size. No relocated
road or other differences are filtered out. The native six-room subset has 768 cases.

The fixture runs six dungeon types, both first-build/rebuild paths, requested
rooms 6/4/8/12/16, seeds 1–64, floor 10 and six party members. Moon Sea and full
retail gameplay/rendering are not covered. This is generator evidence, not proof
of every engine behavior. Run `ModVanillaAudit.SeedMatrix` with `AUDIT_OUTPUT`
set; `AUDIT_MODS` optionally points at an installed-but-disabled package tree.
Without `AUDIT_OUTPUT`, the ordinary test run skips this cross-build export.

## Acceptance before claiming parity

For each remaining domain, bind real native operations behind an explicit mod
request; require ownership, bounded inputs and deterministic conflict policy.
Never add unconditional capacity/behavior changes to make an API name available.
Larger-generation support needs a separate opt-in storage/generation path, or
equally strong same-seed proof that unused support preserves all retail paths.
Publish required-capability declarations only once implementations and tests exist.

Each domain needs enabled-mod tests, no-mod/disabled-mod regression evidence,
failure/unload/slot-reuse tests where relevant, and real gameplay validation before
its coverage can be marked complete. Native ABI and compiler success alone do not
prove plugin capability parity. The matrix above is the completion checklist;
remaining rows must not be described as implemented.

## Validation receipt (Windows, 2026-10-19)

- Clang 23.1.2, x64 Release: `darkcloud` and `darkcloud_tests` built successfully.
- Full suite before the final required-operation preflight addition: 440 discovered,
  432 passed, 8 skipped, no failures. Skips were two mouse-focus environment cases,
  two high-address arena environment cases, two unavailable Vulkan validation-layer
  cases, the Windows symlink privilege case and optional seed export.
- Final affected C++ suite: 48 passed, one symlink privilege skip, no failures.
- Offline Python tooling: 8 tests passed, including mandatory-operation failures,
  conflict propagation, archive contents, and unfiltered seed mismatch detection.
- Real C DLL fixture: separate trust/enable, host calls, failed commit, unload,
  and changed-library rejection. Standalone native example built with Clang.
- Shared-menu PAL Norune offscreen replay: trust warning, confirm, pending restart,
  then trusted active startup verified. Game assets and audit profiles stay local.
- Separate generator export: 3,840 cases each for pinned upstream, empty framework
  and installed-but-disabled Lua/native packages; identical hashes above.
- `ps2/` diff against pinned upstream is empty; Kazarov remains pinned to
  `1a8f9ab705655760ca1cd08ae865c3c852c0846d` without local edits.

The save-field APIs have bounds, transaction rollback, detached snapshot, byte
preservation and RNG tests. This does not substitute for a retail progression
playthrough of every enabled API. Native loading on Linux/macOS/Android remains
unverified. Full capability parity remains outstanding as shown in the matrix.

## Contributor-owned scope

Cutscene, custom item and custom weapon frameworks are reserved for the contributor
per the user instruction. Do not implement competing systems. Their integration
boundaries are documented in [MOD_CONTRIBUTOR_BOUNDARIES.md](MOD_CONTRIBUTOR_BOUNDARIES.md).
Coverage remains pending external integration, not falsely marked complete.

### Retail inventory expansion

Eight operations now cover existing retail item count/grant/removal, weapon grants,
ABS and synthesis-point progression, fitted attachment inspection and native
metadata. Tests use real linked native definitions and verify full-save rollback,
quick-slot accounting, capacity rejection, missing owners, signed overflow,
repeatable explicit grant seeds and preservation of the retail RNG. No custom
item/weapon registration was added. Item use, equip, attachment mutation and weapon
transformations remain incomplete, as shown above.

Retail expansion validation: Clang 23.1.2 Windows x64 Release game/test builds;
446 C++ tests passed, 6 skipped, no failures; all 8 Python tests passed.
An isolated PAL Norune Lua smoke package granted bread, a Bone Rapier and ABS,
then verified committed native values on the following tick. Its PASS log was
visually checked in the shared in-game mod manager. The package and game assets
remain local audit fixtures, not installed gameplay examples. Repeated 3,840-case
empty/disabled-mod exports matched the pinned upstream hash above. No PS2 diff.

### Dependency versions and offline installation

Explicit exact-version pins and SemVer minimum/exclusive-upper bounds now block
incompatible consumers and their dependents before code runs. Runtime and CLI
share the documented comparison policy; the shared menu displays requirements
and mismatch reasons. ZIP installation validates in hidden staging, supports
explicit replacement with retained backups, and leaves enable/trust profiles alone.

Validation: Clang 23.1.2 Windows game/test builds; 43 affected C++ tests passed,
one symlink privilege skip; 13 Python tests passed, including injected failed
publication/rollback and hostile archive paths. A packaged/installed two-mod PAL
Norune fixture showed its incompatible consumer BLOCKED/INACTIVE in the manager.
All 3,840 empty/disabled generator exports again matched pinned upstream. PS2
source remains unchanged. These local workflows do not provide a remote mod store.
