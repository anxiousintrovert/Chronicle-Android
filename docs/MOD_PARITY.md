# Chronicle framework capability coverage

Target: comparable Chronicle modding capabilities, using our own package/API/ABI.
Unchanged MoonMod package compatibility is not a requirement. Reference inspected:
MoonMod `6f5ca66e44ff29f866453b1181c9679aa9b6bd5d`, its API catalog, native SDK
documentation and Chronicle integration. Catalog names alone do not prove that
the reference has a working native implementation of every operation.

**Full capability parity has not been reached.** The initial expansion supplies
a real host-call boundary, 50 implemented operations, native plugins and author
tools. It does not expose unimplemented engine hooks as successful no-ops.

| Area | Current Chronicle framework | Remaining coverage |
|---|---|---|
| Packages and conflicts | Folder discovery, version-constrained dependencies, cycles, duplicate IDs, declared conflicts, exclusive claims and asset collisions; validated offline ZIP install/update with backups; new packages disabled | Automatic remote downloads/update discovery are not implemented |
| In-game management | Shared menu framework, enable/disable, restart preview, reasons, quest progress, errors/logs, permission display, native trust confirmation, owned Lua/native action lists with queued execution/results | Richer journal/skills UI and custom controls |
| Lua and host calls | Isolated bounded Lua, owned tick/map/floor/gameplay/save-loaded listeners, declared-dependency query exports, owned save state/resources, operation discovery, permission and required-operation checks, queued atomic writes | More native events and custom events; mutating dependency commands; native plugin exports |
| Player / party | Joined/active stats and status duration reads; health, thirst, capacities and defense writes; active-player retail status application/removal with native interactions and deferred sounds | Recruitment, native active/town-character switching, temporary tuning ownership |
| Inventory / weapons / economy | Native definition/bag/quick-slot/attachment reads; bounded retail item/attachment grant and removal, weapon grants; ABS/synthesis points and WHP writes; native build-up eligibility queries and transactional status-break; wallet/fishing transactions | Custom item/weapon registration belongs to contributor; item use; weapon equip, upgrade, actual build-up and fitted attachment insertion/removal with active effect refresh |
| World / dungeon | World/floor/restriction and dungeon progress snapshots; story/map/init flag transactions; vanilla generation unchanged | transitions, owned layout/population/loot/floor-rule overrides, back-floor requests, extra floors |
| Entities / combat | No new hooks or engine-capacity changes | Generation-checked handles, spawn/despawn, owned stats/scale, deterministic damage modifiers and kill events |
| Shops / Georama | Saved plot/placement/request inspection; loaded retail element inspection and transactional enable/disable with prerequisites | Owned shop stock/prices with restore, placement/Atla/satisfaction integration |
| Content / assets | Same-size compatible existing asset replacements and package resource reads | Contributor-owned custom item/weapon registration; registered characters/models/animations/maps, structural validation/conversion, native allocation contracts |
| Quests / dialogue / skills | Declared owned objectives, scripted progress/rewards, persisted scalar state | Dialogue trees, native quest events, skill trees, native script interception |
| Rendering / audio | No renderer/timing modifications | Owned overlays/camera/lighting/audio controls, activated only by callers |
| Native plugins | Own C ABI 1 and separate ABI 2; SHA-256 approval binding, separate trust+enable, shared host calls/logging/ticks; ABI 2 observed lifecycle events, owned scalar save values and optional shared-manager actions | Custom/native-ready events, cross-package exports, broader host domains; Linux/macOS/Android verification |
| Author tooling | new/check/inspect/check-set/pack/install/capabilities/apis/compare-seeds CLI; standalone Clang ABI 1 and ABI 2 examples | Engine content editors/converters, rich runtime inspection, automated retail smoke scenarios |
| Saves | Existing namespaced mod trailer, atomic file writes and exact package/version checks | Schema migrations, independent save branching, full NG+ transfer/recruitment/reset services |

Chronicle Two-specific systems (photography, inventions, Ridepod, monster badges,
Spheda) are outside this Chronicle-only framework's scope.

## Georama element validation (2026-10-19)

Two operations inspect and edit existing optional elements in the loaded editor
town. Detached scene state and save fields are staged together; prerequisites and
dependents are enforced in command order. Failed batches publish neither. The
bridge captures native pointers only for an explicit call and publishes on the
same game thread without intervening callbacks. No custom-content registry or
PS2 source change is introduced. JSON host responses now preserve Lua integer
IDs, so returned IDs can round-trip through strict argument validation.

Clang Windows Release game/test builds passed. The full 501-case suite passed
with eight environment/optional skips (493 passed); a subsequently added Lua
integer round-trip regression is checked separately. Five Georama cases cover
ordered prerequisites, whole-save rollback, detached reads/RNG preservation,
missing scenes and malformed inputs. All 13 Python tooling tests passed.
An isolated PAL Norune replay invoked an action in the shared manager, enabled
a retail element, verified both live and saved flags, restored both, and displayed
PASS. This does not verify every element's visual refresh or completion events.
The 3,840-case empty/disabled-mod generation comparison still matches pinned
upstream byte for byte. Cross-platform execution and full parity remain pending.

## Native menu action validation (2026-10-19)

`menu.native.v1` adds an optional C descriptor for ABI 2 plugins, preserving both
published plugin/host layouts. Native and Lua actions share the existing manager,
package namespace, count limit, queue, context cancellation and transaction boundary.
No new menu implementation or engine hook is added. Failed dependencies now unload
dependent native libraries immediately instead of leaving stopped libraries resident.
The standalone lifecycle example includes a native reset-counter action and is
still never installed/enabled by the game build.

Clang 23.1.2 Windows Release game/test builds passed. CTest discovered 496 tests:
488 passed, eight skipped (mouse-focus, high-address arena, unavailable Vulkan
validation, symlink privilege and optional seed export). All 13 Python tooling
tests passed. Six new real-DLL tests cover deferred dispatch without Lua, rollback
and throwing commits, cancellation/reload/unload, shared Lua/native IDs and limits,
invalid UTF-8 metadata, trust/enable gating and dependency-failure cleanup. The
existing ABI 1 and ABI 2 fixtures remain green. The standalone example builds with
Clang and passes offline package check/pack.

An isolated PAL Norune replay trusted/enabled the example, restarted, and ran
Reset visit counter from the shared R1 Actions pane; the manager displayed
Completed. The 3,840-case empty/disabled-mod comparison remains byte-identical to
pinned upstream, and `ps2/` remains unchanged. This is Windows/controller evidence;
other native-loader platforms and mouse activation have not received live replay.
Full parity remains incomplete in the matrix above.

## Player status validation (2026-10-19)

`player.status_add/remove` cover the active character's five retail effects,
native interaction rules and shared timer. Save changes and bounded application
sound notifications are staged together; sounds dispatch only after the entire
callback commits. Calls reject inactive native status objects and benched targets.
No status engine override, new timer, arbitrary status registry or contributor-owned
framework is added. The removal adapter clears the shared counter when neither
freeze nor stamina remains, since only those effects advance it in the engine.

Clang 23.1.2 Windows Release game/test builds passed. CTest discovered 490 tests:
482 passed, eight skipped (mouse-focus, high-address arena, unavailable Vulkan
validation, symlink privilege and optional seed export). All 13 Python tooling
tests passed. New coverage compares the linked native `BtSetStatusErr` with the
staged adapter for 1,920 character/mask/request combinations, verifies RNG and
unrelated-byte preservation, and checks removal, invalid targets, atomic save
rollback and deferred notification counts.

An isolated PAL Divine Beast Cave replay ran the status action through the shared
manager, verified all five effects' application/removal and timer values, restored
clear status and displayed PASS. A town replay verified explicit rejection when
the native active-character status object was unavailable. This does not claim
an audio-listening test or every combat animation/expiration path. No gameplay
package was installed into normal saves or the build. The 3,840-case empty/disabled
generator comparison still matches pinned upstream exactly, and `ps2/` is unchanged.
Linux/macOS/Android execution remains unverified.

## Native ABI 2 validation (2026-10-19)

ABI 2 adds opt-in observed lifecycle callbacks and owned scalar save values through
separate C structs and an entry point; ABI 1 layouts and behavior remain supported.
Callbacks share the existing transaction boundary, save trailer and package
permissions. Start/stop cannot access state or game operations; game-leave can
access owned values only. No new engine hooks, content registry, custom ID range
or contributor-owned cutscene/item/weapon implementation is introduced.

Clang 23.1.2 Windows Release game/test builds passed. CTest discovered 486 tests:
478 passed and eight skipped (two mouse-focus checks, two high-address arena
checks, two unavailable Vulkan validation checks, symlink privilege and optional
seed export). Nine real C DLL fixture tests cover event ordering, save-load/reset,
state validation and budgets, failed callbacks/commits including thrown commit
errors, no-event subscriptions, trust gating and explicit invalid-V2 rejection.
The existing ABI 1 fixture remains green. All 13 Python author-tool tests passed.
The standalone ABI 2 example builds with Clang and passes offline check/pack.
An isolated PAL Norune game replay trusted and enabled that example through the
shared manager, restarted, and displayed `Gameplay entered: visits=1`. Save-load
round trips are covered by runtime fixtures, not an interactive save/load replay.

The 3,840-case empty/disabled-mod comparison remains byte-identical to pinned
upstream. No PS2 source changes. Windows tests do not establish other-platform
loader behavior or full retail-gameplay equivalence. Native exports and
the other remaining domains in the matrix are still outstanding.

## Retail attachment and progression validation (2026-10-19)

Five additional host operations cover attachment bag list/give/remove, native
build-up eligibility and status-break synthesis. Existing attachment/definition
queries now serialize byte-sized stat and socket arrays as numeric arrays instead
of strings. ID 131 is rejected before native lookup because its metadata indexes
past the retail attachment table. No definition registry, capacity change, custom
weapon format, active model reload or PS2 source change is included.

Clang 23.1.2 Windows Release game/test builds passed. CTest discovered 477 tests:
471 passed and six skipped (two high-address arena checks, two unavailable Vulkan
validation checks, symlink privilege and the optional seed exporter). All 13 Python
tool tests passed. The final boundary fix also passed all 22 retail/API tests.
New tests cover atomic bag capacity/overflow handling, default-weapon reset,
status-break rounding and doubled attachments, native flag cancellation, equipped
and under-level rejection, transaction rollback and RNG preservation. Ordinary
build-up targets and eligibility were compared with the native routine for every
retail weapon definition at base/high stats; the two special Mardan weapons have
separate saved fishing-progress boundary tests.

An isolated Windows real-game Lua action granted and inspected Fire and level-3
Attack attachments, removed one, and queried build-up targets through the host.
The shared manager displayed its PASS result. Status-break has native-save fixture
coverage, not an interactive animation/gameplay replay. Other platforms remain
unverified. The 3,840-case empty/disabled-mod seed comparison remains byte-identical
to pinned upstream; this does not establish full retail-gameplay equivalence.

## Menu action validation (2026-10-19)

`menu.actions.v1` lets enabled Lua packages register bounded named actions in the
shared manager. UI input queues work for the existing transaction boundary; no
callback executes during menu input handling. Package lifetime owns registration;
save loads/reset, stopped gameplay, changed world context and failed dependencies
discard or cancel pending requests. This adds no custom content registry or
cutscene implementation.

Clang 23.1.2 Windows Release game/test builds passed. CTest discovered 470 tests:
462 passed, eight skipped (two mouse-capture environment checks, two high-address
arena checks, two unavailable Vulkan validation checks, symlink privilege and the
optional seed exporter). Six new runtime tests cover actions, deferred dispatch,
rollback, stale requests, limits, instruction budget and dependency order. All
13 Python author-tool tests passed. An isolated real-game controller replay opened
R1 Actions, executed a two-coin grant, showed Completed, and verified the actual
wallet increase through a host query. Mouse action controls and other platforms
have not received a live replay.

The separate 3,840-case upstream/empty/installed-but-disabled seed comparison
remains byte-identical (hash below). `ps2/` has no changes against pinned upstream.
The generator comparison does not establish full retail-gameplay equivalence.

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

### Lifecycle subscriptions and dependency query services

Owned startup-registered listeners now cover simulation ticks, observed map/floor
entry, gameplay enter/leave and deferred save-loaded notifications. Read-only Lua
exports allow declared dependencies to exchange detached structured requests and
results with shared invocation/depth limits. These are not native floor-ready,
entity-kill or cutscene hooks, and they do not provide mutating cross-mod commands.

Validation: Clang 23.1.2 Windows Release builds; 458 C++ tests passed, 6 environment/
optional skips, no failures; 13 Python tests passed. New runtime cases cover listener
transaction rollback, non-repeating floor observations, deferred restored-state
notification, detached results, denied writes, shared nested-call budgets and
service lifetime across save-runtime replacement. An isolated PAL Norune package
verified gameplay/map listener ordering and a dependency query in-game; its PASS
log was inspected in the shared mod manager. All 3,840 no-mod/disabled-mod generator
cases still match pinned upstream, and the PS2 diff remains empty.
