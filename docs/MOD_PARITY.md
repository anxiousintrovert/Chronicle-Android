# Chronicle framework capability coverage

Target: comparable Chronicle modding capabilities, using our own package/API/ABI.
Unchanged MoonMod package compatibility is not a requirement. Reference inspected:
MoonMod `6f5ca66e44ff29f866453b1181c9679aa9b6bd5d`, its API catalog, native SDK
documentation and Chronicle integration. Catalog names alone do not prove that
the reference has a working native implementation of every operation.

**Full capability parity has not been reached.** The initial expansion supplies
a real host-call boundary, 81 implemented operations, native plugins and author
tools. It does not expose unimplemented engine hooks as successful no-ops.

| Area | Current Chronicle framework | Remaining coverage |
|---|---|---|
| Packages and conflicts | Folder discovery, version-constrained dependencies, cycles, duplicate IDs, declared conflicts, exclusive claims and asset collisions; validated offline ZIP install/update with backups; new packages disabled | Automatic remote downloads/update discovery are not implemented |
| In-game management | Shared menu framework, enable/disable, restart preview, reasons, quest progress, errors/logs, permission display, native trust confirmation, owned Lua/native action lists with queued execution/results | Richer journal/skills UI and custom controls |
| Lua and host calls | Isolated bounded Lua, owned tick/map/floor/gameplay/save-loaded listeners, declared-dependency query/transactional command exports, owned save state/resources, operation discovery, permission and required-operation checks, queued atomic writes; package-owned deferred custom events | More native events |
| Player / party | Joined/active stats and status duration reads; health, thirst, capacities and defense writes; active-player retail status application/removal; deferred native dungeon character switching with readiness/rejection reasons and full resource reload | Town-character switching, temporary tuning ownership; recruitment remains a separate extension (the pinned reference does not bind it for Chronicle) |
| Inventory / weapons / economy | Native definition/bag/quick-slot/attachment reads; bounded retail item/attachment grant and removal, weapon grants; ABS/synthesis points and WHP writes; native build-up eligibility queries, transactional attachment/progression APIs with native equipped-cache refresh where no new model/effect is needed; unequipped status-break; wallet/fishing transactions | Custom item/weapon registration belongs to contributor; item use; weapon equip, active model-changing build-up, Ruby effect-form resource reloads and equipped status-break |
| World / dungeon | World/floor/restriction and dungeon progress snapshots; story/map/init flag transactions; vanilla generation unchanged | transitions, owned layout/population/loot/floor-rule overrides, back-floor requests, extra floors |
| Entities / combat | Lazy generation-checked live enemy list/get; native combat counts; transactional damage/healing and slow/stop timers; owned live stats and model/shadow scale with conflict detection and cleanup; queued native hit/death dispatch; port-side spawn/clear identity notifications; unchanged retail capacity; active-enemy sphere searches and transactional one-shot movement intent | Spawn/despawn, spawn-time tuning and deterministic damage modifiers |
| Shops / Georama | Owned retail shop stock and shared base prices with conflict detection and conditional restoration; saved plot/placement/request inspection; loaded retail element inspection and transactional enable/disable with prerequisites | Placement/Atla/satisfaction integration |
| Content / assets | Same-size compatible existing asset replacements and package resource reads | Contributor-owned custom item/weapon registration; registered characters/models/animations/maps, structural validation/conversion, native allocation contracts |
| Quests / dialogue / skills | Declared owned objectives, scripted progress/rewards, persisted scalar state | Dialogue trees, native quest events, skill trees, native script interception |
| Rendering / audio | No framework renderer/timing hooks; native enemy model/shadow scale on explicit request | Owned overlays/camera/lighting/audio controls, activated only by callers |
| Native plugins | Own C ABI 1 and separate ABI 2; SHA-256 approval binding, separate trust+enable, shared host calls/logging/ticks; ABI 2 observed lifecycle/custom events, owned scalar save values and optional shared-manager actions; Lua/native dependency queries and commands | Native-ready events, broader host domains; Linux/macOS/Android verification |
| Author tooling | new/check/inspect/check-set/pack/install/capabilities/apis/compare-seeds CLI; standalone Clang ABI 1 and ABI 2 examples | Engine content editors/converters, rich runtime inspection, automated retail smoke scenarios |
| Saves | Existing namespaced mod trailer, atomic file writes and exact package/version checks | Schema migrations, independent save branching, full NG+ transfer/recruitment/reset services |

Chronicle Two-specific systems (photography, inventions, Ridepod, monster badges,
Spheda) are outside this Chronicle-only framework's scope.

## Transactional dependency commands (2026-10-19)

`services.command.v1` supplies explicit Lua command exports and a shared host
operation callable by Lua and native dependents. The caller's transaction now
includes invoked providers' managed state, ordered game commands and owned custom
emissions. Trusted immediate-caller identity supports authorization; provider
permissions and engine ownership apply. Nested invocations share bounded service,
write, emission and currency budgets. Failed providers poison the root transaction
without permanently disabling a reusable provider. Native export descriptors are now supported; this capability does not allocate contributor content IDs.

Validation: Clang 23.1.2 Windows Release game/test builds pass. All 89 affected
runtime/native/API tests passed, including ten command cases covering ordered
writes, trusted caller/provider identities, multi-provider rollback, ignored
errors, invalid results, permissions, shared limits, save/reset rebinding and a
real native DLL invoking Lua commands. The full 648-case suite completed with
641 passes, six skips and one disabled optional benchmark. All 15 Python checks
pass. All 3,840 empty/disabled generator cases match pinned upstream exactly;
`ps2/` remains unchanged. Other platforms remain unverified.

An isolated PAL dungeon shared-manager action invoked a separate provider's
currency grant, checked committed wallet/owned state on the following tick, then
restored both through another command. Its consumer displayed `PASS dependency
commands`. Audit packages and retail assets remain outside the repository.

## Custom event delivery (2026-10-19)

`events.custom.v1` adds initialization-time owned Lua subscriptions, a shared
`events.emit` host call and the additive ABI 2 `CHRONICLE_EVENT_CUSTOM` bit.
Sources are assigned by the runtime; only the sender and declared dependents
receive events. Publication follows successful callback commit. Delivery is
bounded and deferred until the next tick, with detached payloads, per-recipient
rollback and context/load/reset cancellation. No engine hooks or ABI structure
layout changes were introduced. Notifications do not supply synchronous RPC;
transactional dependency commands were added separately, as described above.

Validation: Clang 23.1.2 Windows Release game/test builds pass. All 79 focused
runtime/native/API tests passed, including eight new custom-event cases and a
real native DLL exercising emission, receipt, opt-out and callback failure. The
full 638-case suite completed with 631 passes, six skips and one disabled optional
benchmark. All 15 Python checks and offline dependency/capability validation pass.
All 3,840 empty/disabled generator cases still match pinned upstream exactly.
No PS2 source changed; other platforms remain unverified.
An isolated PAL dungeon replay invoked an event from a provider's shared-manager
action; its separate declared subscriber verified the sender and structured
payload, committed owned state, and displayed `PASS custom events` in the manager.
Audit packages and retail assets remain outside the repository.

## Enemy movement and spatial query validation (2026-10-19)

The native MoonMod binding also exposes `physics.query`, `physics.velocity_get`
and `physics.velocity_set`; these now appear in our catalog and author docs.
Sphere queries select active revealed enemy origins, not collision geometry.
Movement uses native direction/speed without an engine-loop override. Writes
share callback rollback with save/combat changes. There are no persistent
controllers, capacity increases or no-mod behavior changes.

Clang 23.1.2 Windows Release game/test builds pass. The full 629-case suite
completed with 622 passes, six skips and one disabled benchmark. After adding a
native-byte/RNG preservation case, the final affected suite passed all 19 tests,
including seven physics cases. These cover bounds, hidden/inactive/stale actors,
gravity/knockback/stop semantics, mixed transaction failures, command ordering,
slot cleanup and unchanged native bytes/RNG for queries and empty commands.
All 15 Python checks pass. All 3,840 empty/disabled generator cases still match
the pinned upstream exactly; `ps2/` remains unchanged. Other platforms and
collision-constrained movement playthroughs remain unverified. An isolated PAL
dungeon replay invoked all three APIs through a shared-manager Lua action, checked
an exact-origin sphere match, changed and restored movement intent, and displayed
`PASS native movement`. The audit package stays outside the repository.

## Owned enemy scale validation (2026-10-19)

`entities.scale_set` and `entities.scale_reset` stage owner-scoped uniform visual
multipliers alongside save, stat and combat writes. The existing native scale
method updates the main model and shadow; there is no added draw hook. Snapshots
expose the committed native XYZ `visual_scale`. Failed callbacks leave transforms
and ownership unchanged; lifecycle cleanup restores unchanged axes while preserving
later native phase changes. No native layout, capacity or combat-radius changes
are introduced, and no ownership records exist without explicit requests.

Eight focused scale cases passed, covering actual native model/shadow frames,
noncompounding requests, conflicts, mixed transaction rollback, invalid/overflowing
scales, native axis changes, slot/floor cleanup and the no-package game bridge.
They also passed in the full Clang suite described below. An isolated PAL dungeon
shared-manager action verified 2x, then 0.5x, then restored original XYZ values
while checking unchanged health/defense; the manager displayed `PASS owned scale`.
This does not establish every attachment, boss phase or platform's rendering.

## Kazarov dependency alignment (2026-10-19, superseded below)

At this earlier checkpoint the branch matched Chronicle master's Kazarov pin `611182d6393e3717d0700a71f3fd637e63f44d2a`
and its `anxiousintrovert/Kazarov-Android` compatibility source. This is newer than
the prior `1a8f9ab7` pin; it is **not** Kazarov main `04bd301e`, whose removed binary
audio loaders require a separate Chronicle data-format migration. The port uses
the migrated build-script path. A small PNG adapter reuses the shared decoder to
preserve Chronicle's RGB/palette/grayscale UI and localization image support; the
dependency's strict RGBA8 loader rejected the renderer's own RGB PNG output.

Clang Windows Release builds of game, tests and `dcdata` pass. The 623-case working
tree suite (including the scale tests) completed with 616 passes, six skips and
one disabled optional benchmark. The two area-card tests that exposed the PNG
regression now pass without changing their assertions. All 15 Python checks pass;
3,840 empty/disabled generator cases still match the original pinned upstream.
Other-platform and PS2 builds were not run; no PS2 source was edited.

## Owned enemy stat validation (2026-10-19)

`entities.set_stats` and `entities.reset_stats` expose live HP, maximum HP,
defense, experience/money parameters and retail forced drops through the shared
Lua/native transaction boundary. Package identity is assigned by the runtime.
Field conflicts identify the existing owner; failed mixed save/entity batches
publish neither claims nor values. Port-side cleanup handles module failure,
runtime replacement, dungeon exit, floor clearing and native slot reuse. Health
restoration preserves subsequent combat changes proportionally and cannot revive
a dead actor. These calls do not change templates or implement spawn-time tuning.

Clang Windows Release game/test builds passed. The full 536-case run had 528
passes and eight environment/optional skips; all 15 Python tests passed. Nine
focused stat tests passed after the final empty-claim cleanup, including the
game bridge with no running packages. Runtime tests cover trusted ownership for
Lua and a real native plugin. The 3,840-case empty/disabled generator comparison
matches pinned upstream exactly. PS2 source remains unchanged.

An isolated PAL dungeon replay used the shared manager action to change all six
fields, inspect their committed values, reset them and verify their originals;
the manager displayed `PASS owned stats`. This does not validate every boss phase,
reward script or platform. Other-platform execution remains unverified.

## Enemy-kill event validation (2026-10-19)

`events.enemy_killed.v1` provides deferred Lua subscriptions/returned callbacks
and the additive ABI 2 enemy-killed event bit. Capture is dormant without a live
subscriber. Native code captures bounded records only; callbacks dispatch later
with detached payloads and the existing per-callback transaction rules. Stale
floor/unit/load context is discarded, and a 64-record limit reports overflow.
No published native struct layout, PS2 source or retail engine capacity changed.

Clang Windows Release game/test builds passed. The full 522-case suite completed
with 516 passed and six environment/optional skips. Lua tests cover payloads,
stale floors/load, rollback and failed subscriptions; a real native DLL validates
the new payload and failed-callback rollback. A later focused test verifies the
returned callback form without duplicate dispatch. All 15 Python tests passed.
The 3,840-case empty/disabled generator comparison still matches pinned upstream.
An isolated PAL dungeon replay invoked a scripted kill through the shared manager,
received one listener event, committed its owned counter and queued reward, and
displayed PASS. Natural combat/poison producer hooks are source-checked; the live
replay used scripted damage. Other-platform execution remains unverified.

## Scripted damage validation (2026-10-19)

`combat.damage` commits bounded HP changes and deferred hit/death reactions only
after the entire mixed native/save batch succeeds. Invincibility blocks damage;
lethal batches cannot resurrect their target. Pending reactions outlive mod
tracking but expire on native slot construction or floor cleanup. The native
damage adapter preserves all retail statements behind its empty-queue check;
source tests enforce that identity. `combat.get` exposes native floor counts for
observing the deferred bookkeeping. No damage formula or mod kill listener is
claimed yet.

Clang Windows Release game/test builds passed. The full 516-case suite completed
with 508 passed and eight environment/optional skips. Thirteen entity tests
include rollback, invincibility, lethal/no-revival ordering, bounded reaction
capacity, native death bookkeeping exactly once, and slot/floor cancellation.
All 15 Python tests passed, including native source-identity guards. The 3,840-case
empty/disabled generator comparison remains identical to pinned upstream. PS2
source files remain unchanged. Other-platform execution remains unverified.

An isolated PAL Divine Beast Cave shared-manager action verified a one-HP hit,
then lethal damage. The replay closed the menus, allowed native stepping, and
verified one fewer live enemy plus one additional recorded kill before displaying
PASS. This does not establish every boss/death script or every reward/drop path.
`ModSourceIntegrity` now runs the native-body checks as part of CTest as well as
the Python suite.

## Enemy healing and status validation (2026-10-19)

`combat.heal` and `combat.status_add` stage selected enemy fields before publishing
them alongside save/scene transactions. Failed native context, stale handles,
invalid inputs or a later save-command failure leave both domains unchanged.
Unrelated mod callbacks keep the existing save-only path. No damage or death
simulation is introduced, and native timers remain responsible for expiry.

Clang Windows Release game/test builds passed. The full 511-case suite completed
with 505 passed and six environment/optional skips. The final nine entity tests
also passed after adding strict argument-boundary coverage. All 14 Python tests
passed, and all 3,840 empty/disabled generator cases remain identical to upstream.
PS2 sources remain untouched. An isolated PAL Divine Beast Cave action in the
shared manager healed an active enemy to its native cap, applied slow and stop,
verified the committed HP/timers on the next callback and displayed PASS. Partial
health restoration is covered by the native-record tests; the replay does not
establish every combat animation/expiry path or other-platform execution.

## Live enemy inspection validation (2026-10-19)

Entity list/get now expose detached live revealed enemy records with opaque
session-local handles. Tracking is activated by queries; native slot construction
and cleanup invalidate identities. Unit changes and runtime/save replacement also
expire them. The two port lifecycle overrides preserve every native statement;
a source-comparison test checks this against the untouched PS2 originals. They
add only sidecar notifications, which do nothing when no unit is tracked. No
monster capacity, damage formula or spawn behavior is changed.

Clang Windows Release game/test builds passed. The full 507-case suite completed
with 501 passed and six environment/optional skips. A later focused runtime epoch
test checks successful versus rejected state replacement separately. Six entity
tests cover dormant tracking, slot reuse, native cleanup, stale/hidden/dead actors,
unit-state preservation, full-floor native spawn rejection and invalid handles.
The final seven focused entity/epoch tests passed. All 14 Python tests passed, including
the native-body identity guard. The 3,840-case empty/disabled generator comparison
still matches upstream byte for byte. This remains narrower than full gameplay
equivalence. An isolated PAL Divine Beast Cave replay invoked an inspection
action through the shared manager, listed and resolved 15 live enemies, and
displayed PASS. This verifies live reads, not mod spawning, combat writes, or all
native reuse scenarios. Linux/macOS/Android execution remains unverified.

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
loader behavior or full retail-gameplay equivalence. Other remaining domains are tracked in the matrix above.

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
- No generator array expansion, unconditional enemy scans, dungeon patch, RNG hook,
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
results with shared invocation/depth limits. Those lifecycle observations do not replace native floor-ready or cutscene
hooks. Subsequent enemy-kill/custom events and transactional dependency commands
are documented in their separate validation sections above.

Validation: Clang 23.1.2 Windows Release builds; 458 C++ tests passed, 6 environment/
optional skips, no failures; 13 Python tests passed. New runtime cases cover listener
transaction rollback, non-repeating floor observations, deferred restored-state
notification, detached results, denied writes, shared nested-call budgets and
service lifetime across save-runtime replacement. An isolated PAL Norune package
verified gameplay/map listener ordering and a dependency query in-game; its PASS
log was inspected in the shared mod manager. All 3,840 no-mod/disabled-mod generator
cases still match pinned upstream, and the PS2 diff remains empty.

## Native service providers (2026-10-19)

Added ABI 2 query/command descriptors and generic services.query, with no engine
hooks or default-enabled packages. The standalone counter uses owned state and
the shared menu framework; contributor registries remain untouched.

Windows Clang 23.1.2 game/test and example builds passed. Of 654 registered tests,
647 passed, six skipped and one remained disabled; 15 Python tests passed. Six
new native-service cases cover cross-language calls, identity, write rejection,
invalid results, budgets, rollback, descriptors, unload and trust revocation.
Package validation passed. A PAL shared-menu probe displayed PASS native exports
after verifying committed state. All 3,840 upstream/empty/disabled same-seed cases
still match. PS2 files remain unchanged. Full gameplay equivalence and native
loading on other platforms remain unverified.

## Retail build-up mutation (2026-10-19)

`weapons.build_up` now evolves unequipped retail weapons, rechecking native target,
stat/attachment and saved Mardan catch requirements in the transaction. It applies
retail completion values and clears the Mardan counter atomically. No engine hook,
capacity increase, global RNG use, installed gameplay package or contributor
registry change was added. Equipped evolution still needs native model refresh.

Clang 23.1.2 Windows game/tests passed: 657 registered, 650 passed, six skipped,
one disabled. New tests compare every eligible retail target's resulting weapon
bytes against the linked retail completion branch, and cover rejection/rollback
of requirements, attachments, equipped slots, invalid arguments and fish counters.
All 15 Python tests and 3,840 upstream/empty/disabled same-seed cases passed.

A PAL dungeon probe invoked the shared-manager action and displayed PASS retail
build-up after checking the evolved weapon and committed fish-counter reset.
No PS2 files changed. Other-platform runtime behavior remains unverified.

## Retail weapon level-up (2026-10-19)

`weapons.upgrade` supplies an explicit, transactional retail progression action
for unequipped weapons. It uses full ABS or consumes one Powerup Powder, absorbs
fitted attachments with native caps, increments level/synthesis, and rerolls
eligible sockets with an explicit local seed. The global RNG is untouched.
No engine override, automatic progression or contributor registry was added.
Equipped progression remains gated pending active-effect refresh.

Windows Clang 23.1.2 game/test builds passed. Final suite: 661 registered tests,
654 passed, six skipped, one disabled. Four new tests cover native base-stat
completion across retail definitions, deterministic rolls/global RNG isolation,
attachment doubling/caps, payment, rollback, limits and argument validation.
The first run exposed an incorrect saturation expectation in the test fixture;
the corrected fixture and final full suite passed without a production-code fix.
All 15 Python tests and 3,840 upstream/empty/disabled same-seed cases passed.
A PAL shared-manager action displayed PASS retail upgrade after verifying both
Powder and ABS payment paths and committed level changes. PS2 remains unchanged.
Other-platform runtime behavior and equipped effect refresh remain unverified.

## Fitted retail attachments (2026-10-19)

Added weapons.attachment_set and weapons.attachment_remove, moving or swapping
complete owned records on unequipped weapons. Native socket restrictions and
active-element correction apply. Removal requires regular bag space without
unresolved overflow. Changes are atomic with progression and other save writes.
No global RNG, PS2 change, default package, custom-content registry or engine
hook was introduced. Equipped mutation still needs active-effect refresh.

Windows Clang 23.1.2 game/test builds passed. Of 665 registered tests, 658 passed,
six skipped and one remained disabled. Four new tests cover all retail sockets,
record-preserving round trips/swaps, sphere restrictions, bag capacity, rollback,
argument bounds, active-element correction and fit-then-upgrade transactions.
All 15 Python tests and 3,840 upstream/empty/disabled same-seed cases passed.

A PAL shared-manager action displayed PASS fit swap remove after verifying each
committed socket and returned bag record. Other-platform runtime behavior remains
unverified; normal builds install no audit package.

## Native equipped-weapon refresh (2026-10-19)

Added weapons.refresh_state and a native readiness context passed through save
and mixed entity transactions. Equipped upgrades and attachment mutations now
refresh combat values, colour and damaged Lamb's Sword visibility after commit,
without resource loading. Inactive party equipped build-up is also supported.
Active model changes and Ruby effect-form changes reject before publication.
No gameplay mod, custom content registry, PS2 edit or unconditional hook was added.

Five new tests cover readiness schema, successful deferred refresh, failed-batch
rollback, stale contexts, active-model rejection, inactive evolution and Ruby
form restrictions. A PAL probe upgraded the equipped weapon during stable play,
verified the committed level and WHP capacity in the native combat cache, and
showed PASS equipped cache through a shared-manager action. This is not complete
resource reload or all-character visual verification.

Validation: Clang 23.1.2 Windows game/test builds passed. Full suite: 670
registered, 663 passed, six skipped, one disabled. After limiting context capture
to explicit weapon writes, all 84 affected API/inventory/entity tests passed and
the final PAL build repeated PASS equipped cache. All 15 Python tests and 3,840
upstream/empty/disabled same-seed cases passed. PS2 files remain unchanged.

## Owned native shop stock and prices (2026-10-19)

Eight shop operations expose all 18 native lists, their 20-good capacity and
shared base buy/sell prices. List and price-pair claims are exclusive per package;
conflicts identify the existing owner. Mixed shop/save/entity transactions publish
only after all validation succeeds. Owner reset/unload/failure restores unchanged
overrides, preserving external native edits. Writes reject unsafe gameplay phases;
cleanup waits while town menus, conversations or the editor are active.

No PS2 edit, native override, capacity increase, default package or custom-content
registry was added. Stock uses retail definitions and affects the next native
goods rebuild; prices remain global base values, not final per-weapon quotes.

Clang 23.1.2 Windows game/test builds passed. Of 678 registered tests, 671 passed,
six skipped and one was disabled. Eight new tests cover stock/price ownership,
duplicates, capacity, exact restoration, deferred cleanup, external edits,
abandoned staging, atomic failures, readiness, argument bounds and RNG isolation.
All 15 Python author-tool tests and 3,840 upstream/empty/disabled same-seed cases
passed. The seed result covers the existing generator matrix, not all gameplay.

An isolated PAL dungeon probe verified committed native lists/prices and restored
every queried shop entry, then displayed PASS shop restore through the shared
manager action. Actual town purchase/sale UI behavior and other platforms remain
unverified. The audit package is outside the repository and is not installed by
normal builds. Kazarov was rechecked against current official Chronicle and the
fork compatibility branch: both still use this branch's existing 611182d pin.

## Official Kazarov migration (2026-10-19)

The branch now uses unmodified `TheMoonPeople/Kazarov` at
`04bd301efd8486c033caf46faf52f179a6022d7c`; `.gitmodules` points to the official
repository and main branch. It no longer depends on Kazarov-Android or a private
compatibility branch. The pin includes texture logical-size support (a78762c),
the shared skinned-mesh pose fix (43cc847), Android support and frame-capture fixes.
This does not independently establish the reported poncho issue is resolved.

Chronicle still loads its existing extracted retail files. Format adapters
convert HD/BD/SQ audio into upstream semantic JSON/in-memory WAV samples, and the
retail memory-card icon into in-memory glTF/PNG. Upstream owns synthesis, rendering
and platform installation. No Kazarov source is patched, and no renderer is copied.
The retained retail readers are attributed to their source revision and carry the
upstream MIT notice in `port/third_party/kazarov-retail-LICENSE`. A complete change
to upstream's new data.zip/model/config publication format is not claimed here.

Clang 23.1.2 Windows game/test builds passed. The full suite registered 707 tests:
697 passed, nine skipped and one disabled. The newly included upstream fog stress
test exceeded this checkout's old five-second limit; the final suite passed with
the 60-second limit used by current Chronicle CI, without changing test assertions.
The separate six-test retail run passed, checking nine banks, 21 sequences, 367
resolved sample payloads and the existing icon pack. End-of-track metadata is
normalized by the upstream sequence schema; event timing, channel data, metadata
events and payloads are compared. All 15 Python tests and the 3,840-case no-mod/
disabled-mod generator comparison passed. PS2 remains unchanged.

The PAL dungeon probe completed with the official dependency and displayed
PASS shop restore in the shared mod manager; its log reported no unreadable bank
or sequence. This is Windows runtime evidence, not a listening assessment or
Android/Linux/macOS certification. Official Chronicle master is a separate
checkout and still needs its own migration PR; no upstream PR was opened here.

## Native dungeon party switching (2026-10-19)

`party.switch_state` reports gameplay readiness, per-member rejection reasons and
last-request state. `party.set_active` queues an exclusive resource-changing
request, revalidates ownership/native state/assets, then calls the native loader
after the frame closes. It reloads the retail model, weapon/effect and voice;
requests do not merely overwrite the save's current-character byte. A completed
switch is a one-shot gameplay action, not an owned override to restore on disable.
Town selection remains a reference capability gap. Recruitment remains an extension
request, but is not an implemented Chronicle capability in the pinned reference;
see the town integration audit below.

The idle end-of-frame hook returns before accessing native game state or assets.
No shipped package enables this feature. Eight queue tests cover unchanged save
bytes/RNG before execution, exactly-once completion, no-op selection, exclusive
writes/conflicts, invalid native party/weapon state, owner/epoch cancellation,
changed runtime guards, missing assets and loader failures. Clang 23.1.2 Windows
game/test builds pass. The full suite registered 716 tests: 705 passed, ten skipped,
one disabled. All 15 author-tool Python tests pass.

The separately enabled `ModPartyRetail.SixMemberRoundTripThroughNativeResources`
test passes against owned PAL data. Its test-only pump creates a six-member party;
a Lua package switches Toan -> Xiao -> Goro -> Ruby -> Ungaga -> Osmond -> Toan,
allows 45 ticks per character, verifies the native equipped cache after each load,
and executes a verification action through the shared manager's action queue.
It leaves its package/log/screenshot in the chosen disposable audit directory.
Run this test alone, with `DC_DATA` and `DC_PARTY_AUDIT` set; it is skipped otherwise.
No save/disc assets are committed, and the fixture is not part of the game binary.
This does not verify every weapon/effect variant, town switching or other platforms.

The refreshed 3,840-case seed matrix remains byte-identical to pinned upstream
`50eaff633e4e02e5dbc6a7a6a84c7e34688cb738`, with both empty and installed-disabled
mod sets. The `ps2/` diff remains empty. This matrix covers the documented generator
cases, not an assertion of complete retail-gameplay equivalence.

## Town integration audit (2026-10-19; implementation pending)

The reference evidence here is the pinned MoonMod commit named at the top of this
ledger, specifically `ports/chronicle/moonfactory-chronicle.patch`. This is a source
audit, not a runtime comparison or a newly implemented API. The implemented
operation count remains 81. All other outstanding coverage above remains open.

### Reference support versus catalog entries

The reference catalog lists `party.recruit`, but `UnsupportedReason` explicitly
reports that Chronicle has a story-ordered cast and no separate NPC companion
recruitment routine. Its `game.party.recruited` event is tagged `chronicle-two`.
Do not implement an unchecked `party_size` write merely to claim parity with that
catalog name. A future Chronicle recruitment service would need its own story,
initialization and save-state contract. It is not supplied by this audit.

By contrast, `party.town_active` and `party.town_set_active` have Chronicle native
bindings. The latter selects a retail character for walking in a town, separately
from the active dungeon party. Its delayed loader preserves position and rotation.
This is still an implementation target for our framework.

### Integration differences that must be resolved

- The reference increases the town `CharaBuffer` allocation from 115000 to 160000
  quadwords in `EditInit` regardless of mod use. Our existing port initializer
  retains 115000. Any additional storage must be acquired only for an explicit
  request, with defined lifetime and cleanup; do not increase the idle allocation.
- `EdLoadMainChara` resets `CharaBuffer.used` only when its arena argument is null.
  Passing a non-null arena repeatedly does not reset it. Our loader must reset its
  owned arena before reuse and preserve storage while models, texture data or
  queued rendering still refer to it. The native `CDataAlloc2<1>(int)` constructor
  does not allocate backing storage; giving it a larger limit is insufficient.
- The reference remaps town motions through a port override of `GetMotionParam`
  and a build patch targeting `CCharacter::ShadowStep` in PS2 source. The latter
  performs its own motion-range lookup instead of calling `GetMotionParam`.
  Implement both paths outside `ps2/`; do not introduce a generated PS2 patch.
  Preserve native lookup behavior when no town override is active and for every
  other character. The reference's motion map is
  `{0, 1, 2, 0, 0, 34, 35, 31, 32, 0, 0}` for town motion IDs 0 through 10.
- The reference loads `chara/c01d.chr` with `info.cfg` for Toan, and dungeon packs
  `c04b`, `c06b`, `c05a`, `c10b`, `c18a` with `base.cfg` for the other five members.
  Validate resource availability and model/motion assumptions before mutating the
  displayed character. File existence alone does not establish a valid model.
- Town reads must be observational. Separate requested and displayed selection,
  and report readiness or failure instead of silently repairing state in a query.
  Retain dungeon active-member and equipped-weapon state unchanged.

### Required acceptance evidence

Use the shared mod-manager action mechanism and the existing permission/owner
transaction boundaries. The eventual implementation needs exclusive ownership,
queued application outside rendering, epoch and scene revalidation, and explicit
restoration on disable/unload. Repeated switches, owner removal during a pending
switch, conversation/event entry, map changes and save/load must not leave model
pointers or motion remapping attached to stale storage. Restoration may wait for a
safe scene boundary, but the API must report that pending state.

Clang unit tests must cover idle calls, ownership, cancellation and both model and
shadow motion paths. A retail replay must exercise all six characters, repeated
switches, movement, conversations, town transitions and disable/restoration, while
checking that dungeon party/save fields remain unchanged. Inspect rendered output
for model and shadow agreement. Repeat the existing no-mod/disabled-mod seed
matrix and source comparison, supplemented by town-specific idle-state checks;
the dungeon seed matrix alone cannot prove unchanged town behavior.

This work uses existing retail characters only. It must not establish custom
character/item/weapon identifiers or consume the contributor's cutscene, custom
item or custom weapon framework contracts.

## Scoped town motion binding (2026-10-19; town API still pending)

`mods::TownMotionBinding` now provides the internal lifetime token for mapping a
loaded non-Toan retail character's town motions. It is non-copyable, rejects
competing bindings, and releases the mapping on destruction. Only the bound
character in edit mode is affected; outside edit mode, for other characters, for
motions outside the town map, and without a binding, motion IDs pass through.
Construction and resolution do not change native character or save fields.

Both `CCharacter::GetMotionParam` and `CCharacter::ShadowStep` have port-only
adapters. They retain the native function bodies with one scoped resolution call
each. A source test checks both bodies against the read-only PS2 originals. The
shadow needs its own adapter because it searches motion ranges independently.
There are no PS2 patches, header edits, arena increases or production binding
creation in this increment. The operation count remains 81. This is an internal
prerequisite, not a claim that mods can switch town characters yet.

Five focused Clang tests cover dormant identity (including invalid motion IDs),
all five replacement member bindings, other-character and other-mode isolation,
conflicting/invalid bindings, scope cleanup, native model and shadow set/index
selection, and unchanged character bytes/RNG during lookup. The shadow test uses
the native pre-render hold-flag path and a null frame; it does not prove rendered
rig compatibility. The 16 Python author/source tests also pass. Windows Release
game and test builds use llvm-mingw Clang 23.1.2 for C and C++. The final full
suite has 721 registered tests: 710 passed, 10 skipped and one disabled, with no
failures. Retail town switching has not yet been exercised.

The 3,840-case seed comparison remains byte-identical for pinned upstream, empty
framework and disabled-package configurations, SHA-256
`8bdcc22d99722f06fb3bc9006836270a7f965a3ac09e895d51e41ad5c3b6a877`.
This is dungeon generation evidence, not town rendering evidence. The queued
resource loader, exclusive owner/scene lifetime, restoration and retail town
replay from the audit above remain required before publishing town operations.

## Retail town resource and motion audit (2026-10-19)

The opt-in `ModTownRetail.RepeatedBodiesAndMappedMotionsUseBoundedSeparateStorage`
now boots the owned PAL data into Norune, ends the frame loop, and loads all five
replacement bodies three times into a separate arena. It resolves and steps all
11 town motions and their shadows, then restores Toan in the original arena. It
checks unchanged dungeon status bytes and original arena base/limit/usage, stable
replacement arena usage across cycles, and presence of model/shadow motion data.
No retail assets are committed. The test skips unless both `DC_DATA` and
`DC_TOWN_RESOURCE_AUDIT` are supplied; run it alone with a disposable output
folder, which receives config and `resources.json` evidence.

This exposed a real problem with copying the reference motion map unchanged:
Goro's `base.cfg` declares motion 32 as `NONE`, with start/end/speed all zero.
Playing town motion 8 through that slot faulted in native `MotionProc`, reached
from `CCharacter::Step`; LLDB confirmed the stack. Our scoped Goro binding now
uses idle motion 0 for that unavailable town action. The other four characters
retain motion 32. This is an explicit fallback pose, not a newly authored Goro
animation. The change applies only to a bound replacement character in edit mode.

The final retail test passes with a separate arena requested at the original
115000 quadwords, using the existing port's fourfold pointer-size headroom
(460000 host quadwords). Measured used host quadwords are stable in all cycles:

| Character | Used host quadwords |
|---|---:|
| Xiao | 135130 |
| Goro | 82240 |
| Ruby | 88630 |
| Ungaga | 78916 |
| Osmond | 69864 |

These measurements do not justify an unconditional town allocation increase.
They cover these retail packs on this host, not arbitrary replacements, other
platform layouts, or resource release while a scene is running. The test arena
uses the existing process-lifetime arena registry; it does not establish the
production unload contract.

Clang 23.1.2 builds pass. The full suite registered 722 tests: 710 passed, 11
skipped and one disabled; the opt-in retail test passed separately after reducing
its arena request to the original size. All 16 Python tests pass. The refreshed
3,840-case no-mod/disabled-mod seed matrix retains the identical hash above; the
PS2 tree has no differences from the pinned baseline.

This is native load/animation-step evidence, not a rendered town playthrough.
Queued owner-controlled loading, walking/conversation/event behavior, transitions,
restoration on disable, and shared-manager invocation still need implementation
and end-to-end verification before town API publication. The operation count
remains 81 and the broader coverage goal remains open.
