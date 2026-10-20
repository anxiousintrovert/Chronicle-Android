# Chronicle framework capability coverage

Target: comparable Chronicle modding capabilities, using our own package/API/ABI.
Unchanged MoonMod package compatibility is not a requirement. Reference inspected:
MoonMod `6f5ca66e44ff29f866453b1181c9679aa9b6bd5d`, its API catalog, native SDK
documentation and Chronicle integration. Catalog names alone do not prove that
the reference has a working native implementation of every operation.

**Full capability parity has not been reached.** The initial expansion supplies
a real host-call boundary, 135 implemented operations, native plugins and author
tools. It does not expose unimplemented engine hooks as successful no-ops.

| Area | Current Chronicle framework | Remaining coverage |
|---|---|---|
| Packages and conflicts | Folder discovery, version-constrained dependencies, cycles, duplicate IDs, declared conflicts, exclusive claims and asset collisions; validated offline ZIP install/update with backups; new packages disabled | Automatic remote downloads/update discovery are not implemented |
| In-game management | Shared menu framework, enable/disable, restart preview, reasons, quest progress, errors/logs, permission display, native trust confirmation, owned Lua/native action lists with queued execution/results | Richer journal/skills UI and custom controls |
| Lua and host calls | Isolated bounded Lua, owned tick/map/floor/gameplay/save-loaded listeners, declared-dependency query/transactional command exports, owned save state/resources, operation discovery, permission and required-operation checks, queued atomic writes; package-owned deferred custom events | More native events |
| Player / party | Joined/active stats and status duration reads; health, thirst, capacities and defense writes; active-player retail status application/removal; deferred native dungeon character switching with readiness/rejection reasons and full resource reload; owned town body selection and restoration; nine owned dungeon player tuning controls; one-shot native invincibility and combat-timer readback | Broader town interaction and player tuning scenario validation; recruitment remains a separate extension (the pinned reference does not bind it for Chronicle) |
| Inventory / weapons / economy | Native definition/bag/quick-slot/attachment reads; native quick-slot readiness, transactional selection and bounded native action requests; bounded retail item/attachment grant and removal, weapon grants; ABS/synthesis points and WHP writes; native build-up eligibility queries, transactional attachment/progression APIs with native equipped-cache refresh where no new model/effect is needed; unequipped status-break; transactional town weapon loadouts; deterministic retail healing/water/cure item use; wallet/fishing transactions | Custom item/weapon registration belongs to contributor; broader item effects and all-character/item-variant retail validation; live dungeon weapon equip, active model-changing build-up, Ruby effect-form resource reloads and equipped status-break |
| World / dungeon | Owned monster rare/steal loot definitions with per-field conflicts and restoration; owned weighted front/back small/weapon chest pools; world/floor/restriction and dungeon progress snapshots; transactional saved restriction rules with explicit live application; story/map/init flag transactions; native settlement travel, dungeon entry with optional floor selection and native back-floor transport through deferred owner-bound requests; owned future-load dungeon layouts and 2..256 monster requests with conflict/rejection/actual-placement reporting; vanilla generation unchanged | Registered-map and broader floor transitions, broader floor-rule overrides and chest capacity, extra floors |
| Entities / combat | Lazy generation-checked live enemy list/get; native combat counts; transactional damage/healing and slow/stop timers; owned live stats and model/shadow scale with conflict detection and cleanup; queued native hit/death dispatch; port-side spawn/clear identity notifications; retail active limits by default with explicit population expansion to 256; active-enemy sphere searches and transactional one-shot movement intent | Spawn/despawn, spawn-time tuning and deterministic damage modifiers |
| Shops / Georama | Owned retail shop stock and shared base prices with conflict detection and conditional restoration; saved plot/placement/request inspection, transactional request completion with loaded-cache synchronization, derived satisfaction readback, native Atla inspection/atomic collection, and loaded retail placement/removal; loaded retail element inspection and transactional enable/disable with prerequisites | Native condition/reward services; broader placement/Atla retail scenarios |
| Content / assets | Same-size compatible existing asset replacements and package resource reads | Contributor-owned custom item/weapon registration; registered characters/models/animations/maps, structural validation/conversion, native allocation contracts |
| Quests / dialogue / skills | Declared owned objectives, scripted progress/rewards, persisted scalar state | Dialogue trees, native quest events, skill trees, native script interception |
| Rendering / audio | Bounded owned WAV/FLAC/Ogg playback, retail SE dispatch, validated native BGM load/play/stop/state and owned sequence replacements through shared host calls and manager actions; owned front/back lighting and view-scale controls, dungeon camera inspection and bounded owned orbit writes with conditional cleanup, enemy model/shadow scale and temporary minimap reveals on explicit request; no timing changes | Owned overlays, broader camera and audio controls; broader lighting gameplay/platform validation |
| Native plugins | Own C ABI 1 and separate ABI 2; SHA-256 approval binding, separate trust+enable, shared host calls/logging/ticks; ABI 2 observed lifecycle/custom events, owned scalar save values and optional shared-manager actions; Lua/native dependency queries and commands | Native-ready events, broader host domains; Linux/macOS/Android verification |
| Author tooling | new/check/inspect/check-set/pack/install/capabilities/apis/compare-seeds CLI; isolated retail/population/chests/lifecycle/georama/rendering/audio smoke runner with per-scenario evidence; standalone Clang ABI 1 and ABI 2 examples | Engine content editors/converters, rich runtime inspection, broader automated retail scenario coverage |
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

## Town selection ownership coordinator (2026-10-19; internal)

`mods::TownSelection` now coordinates pending selection, exclusive ownership and
restoration independently of native resource storage. It is not yet connected to
the runtime host-operation catalog or the main loop, so it adds no active engine
behavior and the implemented public operation count remains 81.

The coordinator accepts one owned `party.town_set_active` command with an integer
`character` from 0 to 5, rejecting mixed writes/currency and competing owners. It
checks scene readiness, joined membership and asset preflight at enqueue and again
at the post-frame boundary. Selecting the existing member is a no-op; selecting
Toan releases ownership after a successful restoration. An idle coordinator does
not inspect owners, probe files, call a loader or consume RNG.

Owner shutdown, runtime epoch change or removal of the selected party member
creates a restoration obligation. Menus/events or missing resources defer it with
a reason. Failed or throwing native loaders retain that obligation even for the
first switch, because failure may have happened after partial resource changes.
No competing owner can acquire the character until restoration succeeds or the
native adapter acknowledges scene retirement. A scene-token mismatch does not
silently release ownership or run a loader against the new scene. Queries report
state, reason, owner, last successfully selected character, pending target and
restoration status; a failed partial load is not a confirmed displayed character.

Eight Clang tests cover dormant/no-op behavior, delayed loads, exact arguments,
exclusive transactions, conflicts, repeated selections, preflight/readiness
rechecks, cancellation, epoch changes, deferred cleanup, partial-load exceptions,
failed restoration preflight, scene retirement and lost party membership. The
native adapter must still supply verified readiness, scene tokens and actual
resource retirement notifications. These unit tests do not establish those hooks,
real menu/transition cleanup or rendered behavior.

The 16 Python tests pass and the refreshed 3,840-case seed matrix remains identical
to the pinned baseline for empty and disabled mod configurations. No PS2 files
changed. The next integration step is the native post-frame loader/retirement
adapter and then the public host calls through the existing shared manager actions;
all broader coverage gaps remain open.

Validation receipt: llvm-mingw Clang 23.1.2 Release game/tests build passed; full
suite 730 registered, 718 passed, 11 skipped, one disabled, no failures. After
making the coordinator non-copyable, the final rebuild and all eight focused
tests passed again. No production caller instantiates this coordinator yet.

## Native town selection API (2026-10-19)

The coordinator is now connected through `party.town_state` and
`party.town_set_active`, bringing the public operation count to 83. The host
loads retail bodies after `MGEndFrame`, using optional storage allocated on the
first replacement. Selecting Toan or stopping the owner restores the native body
and releases storage; scene retirement clears ownership and storage without
loading against a departed scene. Native loader notification and arena-clear
hooks are port-side only. No-mod hooks return immediately without allocation,
asset probes, owner scans or RNG use. The native town allocation is unchanged.

Explicit visual-record retirement removes renderer pointers into mod storage
before it is reused or unmapped. This fixes the access violation found by the
retail runtime-stop test when storage was first made releasable. Only records
within that storage are removed, preserving unrelated native visual records.

Separate PAL runtime tests select all six bodies through an existing shared-menu
action and Lua host calls, stop the script runtime to restore Toan, and initiate
a native town map transition to retire ownership. They verify zero retained
storage after cleanup and unchanged dungeon character/weapon save fields. Both
pass on Windows with official Kazarov 04bd301e. Coordinator tests cover conflicts
and deferred restoration; an additional API test checks exact schema and dormant
state, and visual retirement tests cover persistent/transient range boundaries.

Movement, conversations, interiors, every native reload path and other platforms
remain unverified. Asset availability is not arbitrary pack validation. The
retail run still logs existing unsupported native event calls. These results do
not establish full MoonMod parity or current upstream Chronicle compatibility.
All broader gaps in the coverage table remain open.

Validation receipt: llvm-mingw Clang 23.1.2 builds passed. The full suite registered
734 tests: 720 passed, 13 skipped and one disabled, with no failures. Both optional
town API retail scenarios passed separately with PAL data. All 17 Python tests
passed and the fresh 3,840-case empty/disabled seed comparison matched pinned
upstream 50eaff63 exactly. No PS2 files changed.

## Player tuning ownership foundation (2026-10-19; internal)

The pinned reference binds nine player tuning controls for Chronicle: dungeon
run speed, Toan swing recovery speed, recoil duration, bonus hit invincibility,
action-gauge refill, thirst drain, weapon wear, map reveal and monster reveal.
Its native implementation scales movement in `port/src/dun/movechara.cpp`, tunes
hit frames at two damage branches, and adds hooks inside native `motionDrive`,
`CUserStatus::Step`, `BattleSubWeaponDmg` and the minimap renderer. The reference's
PS2 patch mechanism will not be copied; any required native replacement must be
implemented and checked outside `ps2/`.

`mods::PlayerTuning` now provides detached transaction state for these controls.
Claims are exclusive per field and expose the conflicting field and package;
independent fields may have different owners. Unlike the reference's implicit
multiplier stacking, this preserves explicit conflict detection and bounded
values. Replacing a request releases its omitted fields; explicit neutral values
still claim their fields. Clear releases only the caller's claims. Runtime epoch
changes and stopped owners retire claims. A failed batch, including one spanning
multiple owners, does not publish partial values or ownership changes. The state
can be copied for staging beside save/entity transactions and swapped at commit.

Six Clang tests cover all nine defaults, exact argument validation, nonfinite and
out-of-range numbers, spoofed owners, per-field conflicts, replacement semantics,
neutral claims, transactional rollback, detached commit, owner shutdown, runtime
restart and dormant RNG preservation. This is an internal foundation only: it has
no production instance, native hook or public catalog registration. Public API
count remains 83. Native integration, lifecycle ordering, baseline comparisons,
retail activation/cleanup and menu-driven examples remain required before these
calls are published. Contributor-owned content registration is untouched.

Validation receipt: Clang 23.1.2 Release game/test builds passed. The full suite
registered 740 tests: 726 passed, 13 skipped and one disabled, with no failures.
All 17 Python checks passed; 3,840 generator cases still match the pinned baseline
with empty and disabled mods. PS2 remains unchanged. This validates the internal
ownership foundation, not the as-yet-unimplemented native tuning controls.

## Native player tuning APIs (2026-10-19)

`player.tuning_state/set/clear` now publish the nine owned controls through the
shared Lua/native host boundary (86 implemented operations). Save/entity/shop
transactions stage tuning on a detached copy and publish only after every write
validates. Ownership cleanup runs before native gameplay and after callbacks,
with an immediate return when no claims exist. No gameplay package is installed.

Port-side hooks cover dungeon movement, both native damage/recoil branches,
action-gauge refill, water drain, base weapon wear and temporary minimap reveals.
The four copied native functions have an executable source comparison requiring
identity to the read-only PS2 bodies after removing only the declared hook calls.
All storage/capacities and native timing remain unchanged. Swing tuning is scoped
to one character step and restores its previous speed afterward; it scales the
native recovery rate rather than replacing it with the reference's absolute
rate. Toan's first swing has no eligible safe tail and remains native. The other
four were exercised with actual PAL motion resources at 0.25, 1 and 4, including
hit boundaries and the guarded completion region.

Native tests cover water drain across dungeon/restriction/weapon modifiers,
weapon wear and its unscaled hit-kind surcharge, dormant/neutral float identity,
mode guards, recoil/invincibility/reveal helper behavior, cleanup and rollback
with save writes. A separate PAL test invokes native motionDrive and verifies
action-gauge values of 10.125, 10.5 and 12 from a starting value of 10 at the three
scales. Another fresh PAL run uses a shared-manager action and Lua host calls to
apply all fields, verify their owners/values, clear, reapply and stop the runtime;
cleanup removes claims before the next gameplay update. Author-tool API discovery
also lists the three operations. Runtime-only claims are not persisted as saves;
previously consumed resources and hit timers are not rewound when tuning stops.

Clang 23.1.2 Windows Release builds passed. The full suite registered 747 tests:
731 passed, 15 skipped and one disabled, with no failures. Both optional retail
tests passed separately. All 18 Python checks passed, and the 3,840-case generator
matrix still matches pinned upstream for empty and disabled mods. No PS2 files
changed. These checks do not prove rendered minimap output, every live movement
or damage scenario, custom motion behavior, other platforms, or full parity.
Those validation gaps and all other unfinished coverage remain open.

## One-shot player invincibility and combat readback (2026-10-19)

The pinned reference's `player.invincible` is now implemented with exactly one
integer `frames` argument, 1–600. `player.combat_state` adds detached readiness,
character, native invincibility/recoil counters and action-gauge inspection. This
brings the catalog to 88 implemented operations. Unavailable dungeon resources,
mismatched native/save player state, invalid party indices and town/title context
reject the write; unavailable reads omit stale combat values and explain why.

Requests stage beside save/entity/shop/tuning writes. Commit only extends the
native timer, preserving a longer existing value and combining multiple requests
by maximum duration. Failed transactions and invalid entity handles publish no
timer or wallet change. A staged transaction does not mutate the engine, and its
commit consumes the pending request rather than allowing accidental replay.
This is not an owned persistent effect: once granted, the native timer expires
normally even after the calling package stops. It does not override every direct
HP mutation, rewind resources, allocate a frame hook or change retail capacities.

Six Clang tests cover schema/permission boundaries, caller identity, detached
read/empty-write byte and RNG preservation, unavailable resources/scenes, native
save identity, monotonic timer extension, mixed-domain rollback, detached staging
and one-shot commit. A fresh PAL run uses an existing shared-manager action and
Lua host calls to grant 600 frames, request one frame without shortening it,
observe 60 ticks of native countdown, stop the runtime and verify continued
countdown afterward. The CLI exposes both operations. Full native enemy-hit
immunity scenarios and cross-platform validation remain open; this does not
complete the other coverage-ledger requirements.

Validation receipt: Clang 23.1.2 Windows Release builds passed. Full suite:
754 registered, 737 passed, 16 skipped and one disabled, with no failures. The
optional PAL host-call/countdown scenario passed separately. All 18 Python
checks passed, and all 3,840 empty/disabled generator cases match pinned upstream.
No PS2 files changed.

## Transactional town weapon loadouts (2026-10-19)

The pinned reference's `weapons.equip` changes the saved loadout only in town;
it explicitly rejects dungeon equipping. Our `weapons.equip` now covers that
capability with required slot selection (0–10) and optional numeric character
(0–5), validating joined membership, native weapon ownership and the existing
equipped index. `weapons.equip_state` reports scene readiness/reason and saved
slots. Catalog count is 90. Live dungeon equipping remains separate unfinished
resource-loading work, along with the other weapon/content integration gaps.

The native bridge supplies an explicit town context to detached save staging.
Ordinary status-only writes and dungeon equipment contexts cannot use it. No
model/cache reload occurs in town; normal dungeon entry reads the selected slot.
Requests share transaction ordering and rollback with other save/domain writes;
a grant followed by equip can see the newly staged weapon. Same-slot selection
preserves every save byte. No change occurs unless a mod requests this write.

Retail validation found that a fresh Norune save retains cur_georama=-1, so
using saved dungeon fields as a town-readiness gate prevented the request. The
adapter instead checks current native mode/map, walking, events/interiors,
loaded town resources and valid party state. It does not rewrite those stale or
uninitialized dungeon fields. This also keeps current-scene readiness separate
from saved equipment data.

Six Clang tests cover schema, all six characters, duplicate copies, slot 10,
whole-save/RNG preservation, unsupported contexts, live dungeon rejection,
empty/incompatible/unjoined selections, invalid native indices, mixed-write
rollback and grant/equip ordering. A fresh PAL run grants a Bone Rapier through
Lua, selects its exact owned slot via a shared-manager action, queues the native
town-to-dungeon transition, and verifies synchronized selected-slot/weapon state
and a loaded weapon frame. The native loader log records `[290] c01w33.chr`.
The test uses ordinary native loading, with no custom weapon or content registry.

This covers one real model-loading transition, not every weapon/effect variant,
all six members in retail play, battle-menu behavior or other platforms. Existing
unsupported native event logs remain in the retail run. Contributor-owned custom
item/weapon/cutscene registration contracts remain untouched.

Validation receipt: Clang 23.1.2 Windows Release builds passed. Full suite:
761 registered, 743 passed, 17 skipped and one disabled, with no failures. The
optional PAL town-to-dungeon test passed separately. All 18 Python checks and
all 3,840 empty/disabled seed comparisons passed. CLI discovery lists both new
operations. PS2 files remain unchanged.

### Transactional retail item use

`items.use` covers the pinned Chronicle reference's supported healing, water and
ailment-cure definitions through our existing host transaction boundary. It
stages native-equivalent effects without mutating the native menu-volume global
or invoking random, sound or live-resource branches. The shared menu action host
can queue it like other game writes; no separate menu implementation is added.

Four focused tests compare complete saves against ItemUseFunc followed by
DeleteItemAfterUseItem for every supported retail definition, all six characters,
and regular-bag versus quick-slot sources. They include pending HP interpolation,
water clamping, combined ailments, stamina and existing timers. They also check
native RNG/menu-volume preservation, callback rollback, grant/use ordering,
invalid schemas, ineffective effects, malformed water/capacity values and
unusable overflow-only items. This is native-function differential evidence,
not a new interactive retail or cross-platform test.

Broader consumable effects and native quick-slot selection/use remain open.
Contributor-owned custom item, weapon and cutscene contracts remain untouched.

Validation receipt: Clang 23.1.2 Windows game/test builds passed. Full suite:
765 registered, 747 passed, 17 skipped and one disabled, with no failures. All
18 Python checks passed; CLI discovery includes items.use. The 3,840 seed cases
match pinned upstream with empty and disabled mods. No PS2 files changed.

### Quick-slot native-path audit

Implementation remains pending. The pinned MoonMod Chronicle adapter selects
one-based slots 1–3 by writing itemNowSel and activeItem.now. It checks active
dungeon play and nonempty saved inventory. Its use operation additionally queues
a four-frame Square-button request in the port's DunMoveChara override; it does
not invoke inventory-menu ItemUseFunc directly.

The native sources establish why these operations need a separate adapter:

- ps2/src/itembombeffect.cpp: checkItemUsed uses a 0.2-point floating water
  threshold, unlike ItemUseFunc's integer-truncated two-point threshold.
- usedActiveItem applies native action healing/drinking and different ailment
  clearing semantics. It can call sound and status globals. It is not a detached
  save transaction helper.
- port/src/dun/movechara.cpp dispatches separate drink, eat, throwable and held
  feather paths. Drinks allocate a hand model and complete across frames;
  throwables enter aiming; feathers consume duration while the button is held.
- CActiveItemPack::CheckStatusType reads the loaded activeItem.item cache, whereas
  checkItemUsed reads saved quick_item_slot. Those are separate state sources.
- Native item loaders in btitem.cpp and menu_dungeon.cpp install the cached item
  and model together. Native DeleteItemModel/DelActiveItem retire the model when
  the final use is consumed. Merely changing saved inventory is not a model load.

The next integration must stage selection/action intent until all callback writes
succeed, validate saved versus loaded slot identity and model readiness, and
route use through native action dispatch. Requests need bounded lifetime and
cancellation on owner stop or scene change; they must not replay into a different
slot after inventory changes. Consumption remains owned by the native action,
with readiness/request outcome distinguished from successful consumption.
Ordinary play must retain its existing input path when no request is pending.

Validation must exercise real loaded quick-slot models, last-item retirement,
drinks, food, thrown items and feather holds; ineffective items; overlapping
physical input; callback rollback; disable/scene cancellation; and empty-mod
behavior. This audit adds no API or hook and is not evidence of implementation
or runtime success. Existing inventory snapshots already expose saved quick-slot
contents; native selection/readiness and action dispatch remain coverage gaps.

### Native quick-slot selection implementation

The selection portion of the preceding audit is now implemented as
items.quick_slots and items.quick_slot_select. The adapter reads saved contents
and reports native context/slot-specific reasons, checks loaded item/model/cache
identity, and publishes both native selection fields only after final save
validation. No action dispatch or new per-frame hook is introduced. No mod calls
means no readiness probe or native selection change.

Four focused Clang tests cover all three native slots, whole-save/model/RNG
preservation, later-command rollback, selected-item removal in the same batch,
owner/schema validation, mismatched models/cache entries, missing resources,
empty slots, busy/event/town/first-person contexts and no-save reads. The tests
use constructed native resource state; real loaded-resource selection and
shared-manager retail smoke validation remain pending alongside action use.
This is selection coverage, not proof of completed quick-slot-use coverage.

Validation receipt: Clang 23.1.2 Windows game/test builds passed. The suite
registered 769 tests: 751 passed after correcting the detached-read tests to
classify items.quick_slots with the existing live-only queries and rerunning
both affected tests; 17 were skipped and one disabled. Dedicated quick-slot
query tests cover byte/RNG preservation. All 18 Python checks and 3,840
empty/disabled seed comparisons passed. CLI discovery lists both operations.
PS2 files remain unchanged. Native action-use and retail smoke coverage remain
outstanding as described above.

### Native quick-slot action requests and retail verification

The action-dispatch gap recorded above is now implemented as items.quick_slot_use.
A validated callback stages one request, revalidates final inventory, then
commits selection and request together. Pending input has an owner, runtime
epoch, save/character/floor identity, selected item and four-frame deadline.
A second pending use rejects. Runtime stop/failure, changed context/slot/model,
expired deadline and arena retirement cancel pending input. Native dispatch
receives held input and at most one press; physical input retains its original
path. No pending request returns immediately without querying native resources
or runtime ownership. No gameplay package is installed by this change.

The request enters existing native drink/eat/throw/feather handling; acceptance
is not proof of consumption or action completion. Once started, a native action
is not undone by disabling its requesting mod. Throwing may leave normal aiming
and player input outstanding. That behavior is deliberate native action parity,
not an autonomous targeting API.

Two additional unit tests cover atomic rollback, pending-use conflicts, one
press, unchanged save bytes before dispatch, deadline expiry and cancellation
on runtime, selected-slot, scene, resource and event changes. A new optional PAL
test, run alone with DC_DATA and DC_QUICK_ITEMS_AUDIT, installs disposable fixture
inventory in town before entering the dungeon through the native transition.
An action registered with the shared manager then selects all three loaded
slots and requests water, bread and feather use through Lua host calls.

The retail test passed: native drinking restores water and removes its temporary
hand model while retaining the cached slot model; eating heals and retires the
last bread model; feather use reduces duration for the bounded hold without
consuming its remaining item. Native dungeon initialization resets gauges, so
the test establishes damaged/thirsty values after the native scene is ready.
No model/resource loader is bypassed. This supersedes the earlier selection-only
smoke-test gap. Thrown-item completion, overlapping physical input, all playable
characters and platform variants still need retail validation.

Validation receipt: Clang 23.1.2 Windows Release builds passed. Full suite:
772 registered, 753 passed, 18 skipped and one disabled, with no failures. The
optional PAL water/food/feather shared-action test passed separately in 9.331 s.
All 18 Python checks and all 3,840 empty/disabled seed comparisons passed.
CLI discovery includes items.quick_slot_use. PS2 files remain unchanged.

### Native throw and physical-input overlap validation

Two additional optional retail scenarios now exercise the same shared-manager
Lua quick-slot use API with a native-loaded Stone slot containing two copies.
The mod-only request produces a native projectile, consumes exactly one copy,
returns to idle, and does not consume another copy during 90 further frames.
The second scenario first selects ineffective food at full HP and establishes a
physical Square hold before requesting the stone slot. It observes native aim
motion, releases physical Square, then verifies the same single consumption,
projectile, retired hand model and no replay. A late press after request dispatch
was not valid evidence of aiming; the final fixture establishes the hold first.

This confirms that the existing native aiming routine reads physical input:
without a hold it advances directly into the throw animation, while a physical
hold preserves aiming until release. No engine adjustment was needed. The tests
cover Toan and Stone, not all throwables, lock-on targets or other characters.

Validation: Clang 23.1.2 rebuilt the test executable. The mod-only throw passed
in 9.157 s, held-input overlap in 9.933 s, and the refactored water/food/feather
scenario in 9.717 s, each in its own process/disposable save directory. All six
quick-item unit tests passed. This commit changes tests and documentation only;
PS2 and engine files are unchanged. The prior full-suite/seed receipt remains
applicable to the unchanged engine, not a newly repeated full-suite claim.

### Native settlement transitions

world.locations, world.transition_state and world.transition cover the pinned
reference's five built-in settlement destinations, using our numeric map IDs.
Town and dungeon requests enter their existing native exit paths after the
display frame ends. MapJump alone only sets NextMapNo/StartEventNo; the adapter
also schedules the town system event or dungeon exit flag, so the current loop
actually exits and performs its native cleanup. No PS2 source override is added.

The request is the callback's sole engine write, is owned, and is revalidated
before dispatch. Pending conflicts report the owner. Owner/epoch, save/scene/
character changes, newly busy contexts and missing destination event resources
reject or cancel without dispatch. Same-town requests are unchanged no-ops.
No queued request means no native context or resource inspection in the new
frame-end hook. Travel cancels pending quick-item input before changing scenes.

Four unit tests exercise schema/catalog, callback isolation, one-shot dispatch,
preflight rechecks, owner/epoch/context cancellation, missing assets, no-op and
busy rejection. Two optional retail tests (DC_DATA and DC_WORLD_TRANSITION_AUDIT,
run separately with disposable roots) initiate Lua calls through a shared-manager
action. Norune-to-Matataki arrives via the native town event. Divine Beast Cave
exit reaches Matataki through native dungeon cleanup; the damaged/poisoned
fixture is recovered on arrival. Both passed. These test one town pair and one
dungeon exit, not all five destinations or all native event/story variants.

Dungeon entry and floor selection, registered destinations, back-floor requests
and floor-generation overrides remain open. This is native settlement capability
coverage, not a claim that those other travel domains are implemented.

Validation receipt: Clang 23.1.2 Windows game/test builds passed. Full suite:
780 registered, 755 passed, 24 skipped and one disabled, with no failures (two
additional mouse-capture tests skipped in this environment). Both optional PAL
travel tests passed separately: town 3.139 s, dungeon exit 11.472 s. All 18 Python
checks and 3,840 empty/disabled seed comparisons passed. CLI discovery lists all
three operations. PS2 files remain unchanged.


### Native dungeon entry and explicit floor selection

`dungeon.locations` reports seven retail dungeons and their native floor counts.
`dungeon.enter` accepts a dungeon ID and optional zero-based floor. Entry uses
the native map transition and entrance menu; an explicit floor uses its existing
direct-entry mode. Omission retains native selection. Requests do not increase
arrays or register custom maps, items, weapons or cutscenes. The catalog now
contains 99 operations.

The floor intent is tied to owner, runtime epoch, save and destination. It can
cross one scene initialization and one entrance call. Seven transition tests
cover schema and native bounds, sole-write isolation, map/floor/owner dispatch,
preflight checks, one-shot consumption and cancellation before/after preparation.
Without a pending intent both native integration points return immediately and
the entrance receives its original -1 argument.

An isolated PAL shared-manager Lua action requested Divine Beast Cave floor 1
(the second floor). The native loader reached ordinary dungeon play on that
floor, and the intent could not be consumed again. The final run passed in
2.136 seconds. This covers one dungeon route, not every dungeon, boss floor,
story variant, dungeon-to-dungeon route or platform. Existing unsupported native
event diagnostics remain present. Registered destinations, back floors, extra
floors and generation overrides remain open.

Validation receipt: Clang 23.1.2 Windows game/test builds passed. Full suite:
784 registered, 760 passed, 23 skipped and one disabled, with no failures.
The PAL explicit-entry test passed separately. All 18 Python checks and 3,840
empty/disabled generator comparisons passed. CLI discovery lists both new
operations. PS2 files remain unchanged. Other platforms remain unverified.


### Back-floor native-path audit

Implementation remains open. The pinned MoonMod Chronicle adapter exposes
`dungeons.back_floor_get` and `dungeons.back_floor_enter`. Entry sets a pending
flag and dungeon ID, then waits until GAME_TASK_SCRIPT_RUN to assign
BT_REQUEST_URA_DUNGEON. It does not immediately travel from ordinary play. Its
inspection returns NowDngMap->gate_key_no and item count; that field is documented
as the floor exit gate key, so it must not be presented as proof that a back-floor
transport requirement has been met.

Native source establishes these integration requirements:

- port/src/dun/movechara.cpp consumes BT_REQUEST_URA_DUNGEON in the script-running
  branch and advances to GAME_TASK_URA_SWAP. The swap loads monster resources,
  changes map/event/random-item pointers and lighting, then returns to
  GAME_TASK_SCRIPT_RUN. It depends on an existing native script continuation.
- ps2/src/btsysscript.cpp `_SET_URA_DUNGEON` merely publishes that request.
  Directly switching BtUraDongeon or gameTask from ordinary play would omit the
  script context and is not an equivalent transition implementation.
- ps2/src/dun/gameloop.cpp generates MainDungeonMap and UraDungeonMap together in
  the normal procedural floor path. BtLoadMonstor indexes the chosen front/back
  enemy-layout table with cur_floor. A non-null dungeon table alone does not
  establish that a particular event/boss floor has a usable back-floor map.
- A pending request must not overwrite an unrelated native request. The native
  swap toggles sides, so replay after arrival could send the player back.

The adapter still needs an owner/epoch/save/dungeon/floor-bound request with
explicit pending/cancellation results, native script-boundary arbitration, and
validation of the current map/resources. An empty queue must bypass these checks.
A retail shared-manager scenario must demonstrate the complete native sequence,
including position and return to play, unchanged inventory when bypassing the
transport requirement, and no replay. Cancellation, existing native requests,
unsupported floors and no-mod paths need separate tests. This audit is source
evidence only; it adds no API or engine hook and does not close the coverage gap.


### Native back-floor transport implementation

The experimental arbitrary-script-boundary failure above is resolved by starting
the actual native transport from ordinary play. The adapter uses the placed
transport event's position/direction and native scene sequence, including its
confirmation and animation. Moon Sea uses script 201; the other six use 200.
An owned in-flight context supplies only that transport's access flag query;
no temporary save flag or inventory write occurs. Normal native flag reads retain
their original behavior without an in-flight transport. The API catalog is 101.

Seven separate PAL first-floor tests passed through shared-manager Lua actions,
checking 90 consecutive normal-play frames after arrival, no pending replay,
byte-identical inventory, and all 50 saved event flags unchanged. Queue tests
cover exclusivity, native request arbitration, owner/epoch/save/floor/map changes,
timeout and one-shot dispatch. A native-function test checks all 50 flag reads,
integer/float arguments and non-pointer outputs without a mod transport.

Other floors, playable characters, regions and platforms remain unverified.
Back-floor exit still uses native gameplay; custom destinations, extra floors,
layout/population/loot and floor-rule overrides remain outstanding.

Validation receipt: Clang 23.1.2 Windows game/test builds passed. Full suite:
796 registered, 763 passed, 32 skipped and one disabled, with no failures.
All 18 Python checks and 3,840 empty/disabled generator comparisons passed.
CLI discovery lists both back-floor operations. PS2 files remain unchanged.


### Transactional dungeon restriction rules

`dungeon.floor_rules_get` and `dungeon.floor_rules_set` expose the native saved
restriction table with explicit dungeon/floor coordinates and optional live
application. Unlike the reference setter, a saved edit does not implicitly
change another floor's active restriction. Native table bounds and defined
rule IDs are validated; applying a character rule requires that joined member
to already be active. These persistent save edits are not an ownership overlay
and are not automatically undone on disable. Catalog count is 103.

Three focused cases cover table-only byte changes, cross-domain rollback,
matching live context and character requirements, native bounds and invalid
rules. An isolated PAL shared-manager Lua action changed a remote floor,
verified unchanged active restriction, applied thirst restriction to the current
floor, then restored both table entries and active rule. The complete table and
active restriction matched their originals. The retail run passed in 8.430 s.
This validates transactions/readback, not every restriction's gameplay effect,
character/floor combination, region or platform. Generation and engine capacities
remain unchanged; arbitrary owned floor-rule overlays remain separate work.

Validation receipt: Clang 23.1.2 Windows game/test builds passed. Full suite:
800 registered, 766 passed, 33 skipped and one disabled, with no failures.
The optional PAL manager-action test passed separately. All 18 Python checks
and 3,840 empty/disabled generator comparisons passed. Both operations appear
in CLI discovery; PS2 files remain unchanged.


## Monster loot definition integration audit

Historical pre-implementation audit; the implementation receipt below supersedes
the open status for monster definition operations. Chest loot remains open. In pinned MoonMod commit
`6f5ca66e44ff29f866453b1181c9679aa9b6bd5d`, `OverrideDungeonLoot` changes
only `MONSTOR_MODEL::rare_item` and `steal_item`; its query also exposes
`attachment_kind` and `drops_items`. This is monster definition coverage,
not chest loot customization. Its numeric -1..450 check is insufficient as
native item-definition validation.

The Chronicle native paths establish the following integration requirements:

- `ps2/src/monstorunit.cpp`, `SetupBaseModel` and `SetMonster`: definitions
  are copied into the loaded model cache and then live actors. Definition
  overrides affect subsequent model loads, not already-loaded actors. Reset
  and owner retirement likewise cannot promise to rewrite existing actors.
- The same file's rare-drop path preserves an existing forced drop and checks
  `drops_items`; the steal path checks inventory acceptance before starting
  its pickup. The adapter must retain these native probabilities and gates.
- `port/src/dun/movechara.cpp`: ordinary random drops and stolen items reach
  `BtGetAttach_Init`, while designated gate keys use their native presentation.
- `ps2/src/btitem.cpp`, `BtGetAttach_Init`: attachment volume is generated only
  for the native attachment range; pickup calls `CDngStatusData::GetItem`.
- `ps2/src/dngstatusdata.cpp`: `CheckItemGet` checks dungeon-item and attachment
  capacity but has no weapon-capacity branch. `GetItem` indexes weapon data by
  item ID and can use an eleventh weapon slot. Therefore a numeric upper bound
  alone does not establish a valid or capacity-safe weapon drop. Actual native
  definitions, attachment payload requirements and full-inventory behavior
  need validation before claiming supported drop categories.

The planned adapter needs per-field ownership, informative conflicts, atomic
staging with other host writes, conditional restoration that preserves later
external edits, and cleanup on owner/runtime retirement. Read-only family and
drop-gate fields must not be mistaken for interchangeable loot identifiers.
No claims or native table changes should exist without an explicit request.
Contributor-owned custom item/weapon registration remains a separate extension.

Required evidence includes whole-definition preservation outside requested
fields, unchanged RNG for queries/empty batches, disjoint-owner coexistence,
same-field conflicts, mixed-command rollback, retirement restoration, and a
shared-manager retail scenario crossing a real model load. Pickup validation
must include full inventory and weapon slots, not just table readback. This
audit is source evidence only; it adds no API and makes no runtime-success claim.

### Owned monster loot implementation

`dungeon.loot_get`, `dungeon.loot_set` and `dungeon.loot_reset` now cover the
pinned reference's native rare/steal definition operations. Catalog count is
106. Each field has independent package ownership, conflict diagnostics, staged
commit alongside other host writes and conditional restoration on reset or
runtime/owner retirement. Native probabilities, forced drops, family and drop
gates are untouched. No drop-path hook or native capacity change is introduced.

Eight focused tests cover schema/definition validation, all-table and save-byte
preservation, unchanged RNG for reads and empty/abandoned transactions, disjoint
owners, conflicts, ordered transfer, external edits, failed save/entity batches,
owner cleanup before the no-gameplay early return and native pickup capacity.
The capacity test confirms that full ordinary/attachment bags reject pickup,
but full weapon arrays pass the initial native check and fail the later grant.
The API preserves and documents this native limitation rather than promising
delivery or changing every pickup's behavior.

An optional PAL Windows test creates a disposable package and uses a shared-menu
action to override definitions in town, then requests native dungeon entry.
Loaded model caches and active monsters receive the requested rare/steal items.
Resetting all definitions restores the entire native table while the loaded
monsters retain their copies. The final run passed in 11.705 seconds. This is
native load/readback/restoration evidence, not an all-item drop/steal playthrough.
Other floors, characters, regional assets and platforms remain unverified.
Chest loot, custom item registration and spawn-time tuning remain separate work.

Validation: Clang 23.1.2 Windows Release game/test builds passed. The final suite
registered 809 tests: 774 passed, 34 skipped and one disabled, with no failures.
The PAL scenario passed separately. All 18 Python checks and all 3,840
empty/disabled generator comparisons passed. CLI discovery lists the three new
operations. Protected PS2 files remain unchanged; no gameplay package is shipped.

### Expanded dungeon layout dependency audit

Layout implementation remains open. Pinned MoonMod `SetDungeonLayout` requests
2-64 rooms, area 8-46 and room sides 3-6. Room counts are placement targets, not
a guarantee that packing succeeds. Its empty request releases that owner's
configuration; its owner list uses the most recently submitted configuration.
Our implementation needs explicit ownership/conflict reporting instead of
silently replacing another package's layout. A retail-capacity-only setter
would not close this coverage gap.

Source inspection identifies coupled capacities and consumers:

- `ps2/include/dungeonmap.hpp`: 400 cells and mask entries, 16 rooms and
  room-discovery entries, 48 events, 24 boxes and a fixed class-size assertion.
- `ps2/src/dungeonmap.cpp`: `BUILD_MAP_INFO::link[16]`, two 400-cell work arrays,
  `roomStack[16]`, and a distinct 256-entry room-link scan. Grid addressing uses
  stride 20, while room-link scanning uses stride 16. Neither can be changed
  by replacing every occurrence of one numeric constant globally.
- `buildRandomMap`: native placement consumes RNG for room sides 3-4 in a
  16-cell build area, attempts initial rooms before joining them, and copies
  exactly 0x100 bytes of room data. Capacity, copy size, retry behavior and RNG
  order must be considered together. Stair/back-floor placement and room-link
  generation follow the room-building phase.
- `port/src/dungeonmap.cpp`, `ps2/src/dungeoneventman.cpp` and
  `ps2/src/dungeonparts.cpp`: character doors, event positioning and collision
  lookup also address the grid directly. Rendering/minimap code has separate
  cell loops, tile geometry and object-coordinate calculations.

The reference expands the grid to 48, rooms to 64, events to 384 and boxes to 64
through generated source/header patches. It also adds a separate optional exit
room; that is gameplay behavior, not a prerequisite for larger storage, and
must remain opt-in. Layout, population and exit-room ownership must stay distinct.

Our current CMake pipeline strips overridden functions into build-directory
source copies and compiles against original native headers. A consistent port
header/source overlay or another complete storage abstraction is required for
expanded layouts; a single replacement function is insufficient. Any generated
overlay must remain outside protected PS2 directories and fail clearly when
upstream source assumptions change. All translation units must see the same
class definition, including transitive quoted includes.

Acceptance must retain the pinned empty/disabled-mod seed matrix and native RNG
results, add requested-layout connectivity/bounds/door/stair tests, and exercise
collision, minimap, back-floor travel and resource limits in retail scenes.
If storage dimensions change, retain the raw audit output and separately compare
semantic coordinates; do not drop extra occupied cells or room/link differences
to make the comparison pass. This audit changes no engine behavior and does not
claim that expanded generation or its no-mod invariants have been validated.

### Checked native overlay tooling

`tools/native_overlay.py` provides the build-copy foundation for the expanded
layout work. It mirrors the complete native `include/` and `src/` trees into
a separate output tree, preserving unpatched bytes. Mirroring all headers lets
transitive quoted includes resolve to the same generated definitions. This tool
was initially standalone; the CMake integration below now uses it. It does not expand any engine capacity or
publish an additional host operation.

Usage from the repository root:

```sh
python tools/native_overlay.py --source-root ps2 --output-root build/native-overlay --manifest path/to/overlay.json
```

The manifest has exactly `schema_version: 1` and a `files` object. Each key is a
canonical `include/...` or `src/...` path; each entry contains `sha256` and a
nonempty `replacements` array. Each replacement specifies nonempty `old`, `new`
and a positive exact `count`, applied in order. Hashes cover UTF-8 text with CRLF
normalized to LF, so checkout line endings do not invalidate the pin. An empty
`files` object produces an identity mirror. `--check` validates without writing.

All hashes, replacement counts and destination paths are checked before output
changes begin. Source/output overlap, PS2 output directories, path traversal,
symlinks/junctions and unmanaged-file overwrites reject. An ownership index
tracks generated files so removed inputs cannot leave stale managed headers;
unrelated files are preserved. Unchanged copies keep their timestamps. Output
files are replaced individually; the tool is not a whole-tree atomic deployment
and must finish successfully before compilation starts. Upstream hash mismatches
require reviewing the affected transformation before updating the pin.

Validation: nine focused Python cases cover identity copies, patching without
input writes, line-ending normalization, stale pins/counts, dry runs, protected
paths, stale/unmanaged outputs, invalid ownership indexes and schemas, and
simulated link rejection. All 27 Python checks pass. A real 322-file identity
mirror compiled its dungeon/monster headers with llvm-mingw Clang 23.1.2 and
asserted retail room/cell capacities. No original PS2 file changed. This proves
the copy/check mechanism and header compilation, not expanded-layout runtime
behavior or complete build integration; those remain the next implementation work.

### Native overlay build integration

The PC CMake build now generates the checked `native-overlay/include` and
`native-overlay/src` trees beneath its binary directory. All native stripping,
override-signature checks and native/port/test compilation consume that shared
copy. The original native tree remains the input for file discovery and build
dependencies, never an output. The checked-in `port/native_overlay.json` is an
identity manifest: no capacities, statements or RNG behavior change here.

Generation runs during configuration so headers exist for compiler checks, and
as a prerequisite of native consumers during builds. Explicit byproducts let
Ninja recreate a missing generated source; input, manifest or generator changes
rerun generation. Unchanged mirrored files keep timestamps. A repeated build
performs no C++ compilation; only the existing asset-copy steps still run.

Validation: llvm-mingw Clang 23.1.2 rebuilt game and tests successfully. All 322
mirror files were byte-identical to their native inputs; inspection of 380
engine/test C++ compilation commands found the generated include tree and no
original native include path. Standalone C plugin fixtures use only the plugin
SDK and do not need native headers. The full suite registered 809 tests: 774
passed, 34 skipped and one disabled. All 27 Python checks and all 3,840 pinned
upstream/empty/disabled seed comparisons passed. A missing generated dungeon-map
source was recreated by the build without changing the protected original.

Expanded layout transformations, ownership APIs, requested-layout runtime tests
and non-Windows builds remain outstanding. This integrates the build mechanism;
it does not claim expanded-layout capability parity.

### Expanded room storage foundation

The checked overlay now raises native room descriptors, room-discovery flags,
builder room stacks and per-cell room membership from 16 to 64. Exact patches
update their initialization, copying and membership loops, and enlarge the
room-link candidate list. The 20x20 cell grid, 16-cell build area, room-size RNG
and caller-selected retail room counts are unchanged. There is no layout API
yet: expanded grid addressing, collision/minimap handling and owned generation
requests remain required to support the full target. This is an intermediate
storage change, not a substitute for those capabilities.

Storage has a measurable default footprint: on the tested Windows build,
`sizeof(CDungeonMap)` increases from 76,576 to 77,536 bytes. The two global
400-cell builder arrays also gain 153,600 bytes of room-membership storage in
total, and the global room stack gains 768 bytes. These are allocated capacities,
not extra generated rooms or enabled gameplay mods. Native PS2 files remain
unchanged; the transformations apply only to the port build copies.

The raw seed exports no longer match byte for byte because they include
`map_bytes`. Across all 3,840 cases, that field is the only parsed difference
for both empty and disabled frameworks. Rooms, every occupied cell, links,
boxes, grid dimensions, gate keys, seed and next RNG value all match the pinned
upstream. Raw hashes and mismatch results are retained. The author tool's
explicit `--allow-map-size-change` comparison reports both sizes and rejects
every other field difference, including extra occupied cells and truncation.
Its default comparison remains byte-for-byte strict.

Validation: Clang 23.1.2 Windows game/test builds passed. Two focused cases verify
64-slot clearing without RNG/cell changes and native visibility handling of
room slot 63. The full suite registered 811 tests: 776 passed, 34 skipped and
one disabled. All seven PAL first-floor back-floor scenarios passed separately,
covering native front/back loading and travel with the new class layout. All
28 Python checks passed. These results do not prove all floors, platforms or
larger-layout generation; those validation requirements remain open.

### Expanded grid storage foundation

Port build copies now use a shared 48-cell storage stride for the cell/mask
arrays and builder workspaces. Rendering, collision, first-person movement,
events and generator indexing use that stride. Retail generation still uses
its original build area, room sizes and RNG order; no layout API is exposed.
Active bounds, room-link scanning and requested large-floor generation remain
unfinished, so this does not yet establish expanded-layout capability parity.

Moon Sea scenery has legacy row-wrapping and adjacent-field reads outside its
logical 20x20 cell array. A dedicated retail-layout reader preserves those
values through object-byte reads, including the original 16-room suffix,
instead of allowing the new stride to alter scenery acceptance and RNG.
Future expanded-layout requests require independently bounded sampling.

The initial retail replay caught missed old-stride accesses in normal player
collision and first-person movement. Both port consumers now use the shared
stride. After that correction, all seven PAL first-floor back-floor scenarios
pass, including 90 consecutive normal-play frames after transport and unchanged
inventory/event flags. This is not a full first-person or all-floor playthrough.

Validation: llvm-mingw Clang 23.1.2 game/test builds pass. The full suite has
812 registered tests: 777 passed, 34 skipped and one disabled. All 28 Python
checks pass. Across 3,840 cases each, empty and disabled frameworks match every
exported upstream field except map size (76,576 versus 115,616 bytes).
Exports inspect all 48x48 storage cells, including any unintended occupied cells.
Raw hashes remain different and the explicit map-size-only comparison passes.
Protected PS2 sources remain unchanged. Other platforms and larger requested
layouts remain unverified.

### Room-link workspace preparation

The special-door/key flood-fill workspace now has 48x48 storage and uses the
same storage stride as the map. Its retail scan limits, candidate order and
32-door selection cap remain unchanged. This removes a separate 16-cell
addressing dependency; it does not enable larger layout requests.

The native builder assumes two initial rooms exist after bounded placement
retries and calls `joinRoom(0, 1)` without checking that assumption. Exposing
custom build areas and room sizes must address failed initial packing before
entering that path. A successful configuration write cannot promise that a
requested room count can fit. Custom-generation failure handling, active scan
bounds and connectivity/key-reachability tests remain required.

Validation: Clang 23.1.2 game/test builds pass. The 3,840-case empty/disabled
seed exports retain the previous grid-storage hashes, with map size still the
only difference from pinned upstream. PS2 originals remain read-only.
The full suite again has 777 passes, 34 skips and one disabled test; all 28
Python checks pass. No new public operation or gameplay package is added.

### Explicit generation context (internal)

`chronicle/dungeon_build.hpp` supplies a scoped, validated internal layout
context: 2-64 target rooms, area 8-46 and room sides 3-6. Without that context,
native room counts, area 16, sides 3-4 and retail scan limits remain in effect.
Checked build-copy transformations consume the context for placement, part
filtering, stair/back-floor candidate collection, special-door flood fill and
character-door candidate scans. Candidate arrays can hold the full grid.

Custom builds reject failed initial room packing before joining nonexistent
rooms and reject missing stair/entrance candidates. Exception unwinding restores
the previous context. These checks do not make the partially generated map
transactional: a future host adapter must discard it and handle failure before
publishing a floor. No public layout operation or manager action is exposed yet.

The native back-floor road overwrote column 19 even in a larger layout; a test
caught it deleting the upper stair. Custom builds place that road at column 47,
while retail builds retain column 19. Live transport script coordinates and
per-map collision/render/minimap bounds still need integration. Moon Sea's custom
scenery placement also needs a separately bounded sampling path.

Two native generation tests cover impossible initial packing with context
restoration, and 192 expanded builds across six dungeon types. Each expanded
case targets 64 rooms with sides 3-6 in area 46, creates more than 16 rooms,
occupies space beyond the retail grid, retains both stairs, and connects all
room centers and the upper stair to the lower stair in the occupied-cell graph.
This establishes structural connectivity, not physical collision traversal,
locked-door/key accessibility or valid native resource allocation.

The empty/disabled 3,840-case exports keep the previous hashes and differ from
pinned upstream only in map size. PS2 originals remain unchanged. Public
ownership/transaction APIs, failed-build recovery, live large-floor scenarios,
remaining dimension combinations and other platforms remain outstanding.
Clang 23.1.2 Windows game/test builds pass. The full suite has 779 passes,
34 skips and one disabled test; all 28 Python checks and all seven separate
PAL first-floor retail back-floor scenarios pass.

### Per-map runtime dimensions

Successful internal generation now publishes an extent on that map. Native
map copies retain it, and native initialization/submap initialization reset it
to retail bounds. The field is appended in the port header copy; the original
PS2 class is untouched. Runtime bounds do not depend on a temporary build scope.

Map drawing/precalculation, fire/raster/water readers, normal player collision,
native neighbourhood collision, first-person movement and camera collectors
consume the persisted bounds. The existing first-person collision fixture now
also exercises a floor and wall at cell 43,43: retail bounds exclude that cell,
expanded bounds retain floor contact and stop movement at the wall. The camera
collector likewise sees the far cell only with expanded bounds. Fixture
vertices now have positional homogeneous coordinates so translations are
actually exercised. Map-copy/reset tests and the 192 generation cases pass.

This is not yet complete large-floor gameplay integration. Minimap scaling,
native event collection/capacity, transport script coordinates, spawn placement,
custom scenery and resource validation remain required, along with the public
owned layout API and failed-build recovery. Rendering bounds are source/build
verified; the synthetic collision test is not a retail large-floor playthrough.

Validation: Clang 23.1.2 Windows game/test builds, the full 779-pass suite
(34 skipped, one disabled), all 28 Python checks and all seven separate retail
back-floor scenarios pass. The 3,840-case empty/disabled audit now reads the
actual per-map extent; all exported fields except map size match upstream.
The Windows map size is 115,632 bytes versus the baseline's 76,576. No protected
PS2 file changed; other platforms remain unverified.

### Expanded-floor minimap

Maps carrying the explicit 48-cell extent now use a separate minimap projection:
five pixels per tile, fitting the complete grid in a 240x240 region at the
existing origin. Tiles and player/trap/chest/Atla markers share that projection.
Player and object symbols retain readable native sizes, and the native atlas,
rotation, opacity modes, discovery masks and separate map/crystal reveal rules
remain in use. Invalid/nonfinite object coordinates cannot index the map arrays.
The retail branch retains its previous statements and arithmetic; a source
identity check permits only the explicit expanded-map dispatch and existing
owned reveal hook.

Three tests cover all 2,304 tiles, far-edge alignment, discovery/reveal
independence, inactive/open objects, malformed coordinates/part IDs, hidden UI,
and unchanged map bytes/input/RNG during draw-list construction. A Vulkan
readback test calls native `DrawMiniMap` with synthetic atlases and verifies the
far tile, marker overlay and unchanged reveal flags. Its optional
`DC_MINIMAP_AUDIT` image was visually inspected: the full synthetic grid and
far-edge markers fit onscreen. This is projection/renderer evidence, not a
retail-atlas large-floor playthrough or proof of every display configuration.

Public layout ownership and failure recovery, expanded native event capacity,
transport script coordinates, spawn placement, custom scenery and resource
validation remain outstanding. The operation catalog remains at 106.
Validation: Clang 23.1.2 Windows game/test builds pass. The full suite has 784
passes, 32 skips and one disabled test; all 28 Python checks pass. Both
3,840-case empty/disabled seed exports retain their prior hashes and match
pinned upstream except for map size. PS2 sources remain unchanged.

### Expanded runtime event manager

The port runtime event manager now stores 384 records, but its active limit
remains 96 for retail maps. Expanded grid setup scans all cells and enables the
larger limit only after preflight counts every matching definition against
available records and checks placement/trigger frames. Rejection occurs before
publishing slots or moving frames, avoiding the native exhaustion spin for
custom floors. The floor adapter still needs to handle such rejection before
publishing a complete playable map.

Native manager resets clear the full backing storage and reset the active
limit. Event searches, switches, holds and mod transport lookup use that limit.
The collision-hit array is independently fixed at 96: its native scan remains
unchanged, enforced by a source identity test. The added storage has a default
memory cost; extra runtime slots do not become active without an expanded map.

Three native tests demonstrate retail exclusion of far cells/extra records,
placement through slot 383 with correct world coordinates and working native
position/switch/hold queries, and atomic rejection of exhaustion or missing
frames. All seven separate PAL retail back-floor scenarios pass with the new
manager layout. These use retail-sized maps, not a complete expanded-floor
playthrough.

This runtime manager is distinct from `CDungeonMap::events`, whose 48 placed
map-event records still require their own expansion and active limits.
Definition slots remain at 64. Transport script coordinates, public layout
ownership/failure recovery, spawn placement and custom resource validation
remain outstanding. The public operation count is unchanged.
Validation: Clang 23.1.2 Windows game/test builds pass; the full suite has
785 passes, 34 skips and one disabled test. All 29 Python checks pass.
The empty/disabled 3,840-case exports retain their prior hashes and still
match pinned upstream except for map size. No PS2 originals were edited.

### Expanded placed map events

`CDungeonMap::events` now has 384 backing records, separately from runtime script
events. Allocation, activation and mimic cleanup use 384 only for maps with
expanded dimensions; retail maps retain a 48-record active limit. Native map
initialization and generation clear the full backing storage. Chest and Atla
object capacities remain unchanged and are not implied by the larger event array.

Custom chest/mimic/Atla allocation checks event availability before changing
objects. Direct Atla placement also rejects an exhausted eight-object array.
Native floor-generation Atla placement checks event space before publishing that
object; full floor generation still needs a staging/recovery adapter. These
guards do not alter retail exhaustion behavior.

Enlarging the array moves the cells that follow it. The Moon Sea compatibility
reader therefore explicitly addresses the original 48-event prefix boundary,
not the new physical tail. A regression test plants different values at both
boundaries and verifies the retail value is read.

Native tests place a chest in event slot 383, activate it by position, verify
retail exclusion, exercise expanded mimic cleanup and confirm initialization
resets every slot. Exhausted event/Atla allocation preserves the entire map.
All seven PAL retail back-floor scenarios pass with the new object layout.
The 3,840-case empty/disabled comparisons match all upstream fields except map
size: 142,512 bytes on this Windows build versus the baseline's 76,576.
The additional capacity consumes memory by default but does not increase
retail object counts or active event limits. Protected PS2 files remain unchanged.

Transport coordinates, complete large-floor resource/spawn handling, custom
scenery and the owned public layout API with failure recovery remain unfinished.


The native debug floor-reset path also clears all placed and runtime event
records and restores the retail runtime limit before setup. The independent
96-entry collision-hit storage is unchanged.

Validation: Clang 23.1.2 Windows game/test builds pass; the full suite has
787 passes, 34 skips and one disabled benchmark. All 29 Python checks pass.
Both 3,840-case empty/disabled exports differ from pinned upstream only in
map memory size. Other platforms and complete expanded-floor gameplay remain
unverified.


### Expanded Moon Sea scenery

Explicit custom generation now samples the full 48-cell grid for Moon Sea
scenery. Footprints outside the grid or touching the reserved transport-road
column are rejected. Entrance clearance scans the full custom grid; placement
still uses the native eight-object limit and bounded retry count. Without a
custom build scope, the original coordinate range, legacy layout reader and
20-cell entrance scan remain in use.

A native placement test covers 64 seeds, far-side placement and entrance
clearance, footprint bounds, the reserved road, and an entirely occupied map
that terminates with no scenery. Existing 192-case custom generation checks
also pass. This is placement evidence, not a complete custom-floor playthrough.
Transport scripts, population/resources and the public owned layout API with
failure recovery remain outstanding.

Validation: Clang 23.1.2 Windows game/test builds and the full suite pass
(788 passed, 34 skipped, 1 disabled). All 29 Python checks pass. Empty and
disabled 3,840-case seed exports retain their previous hashes and match pinned
upstream except for map memory size. No PS2 originals changed.


### Expanded object placement

Chest/trap/Atla generation and enemy arrangement now select placement geometry
across the explicit map extent. Retail maps delegate to the original 20-cell
sampler. Expanded maps sample uniformly from all native placement rectangles and
apply their rotation and point bounds, avoiding the native sampler's
196-rectangle whole-floor truncation and rounded out-of-range selection. Missing or invalid geometry is rejected
before output/RNG changes. Expanded chest/Atla retries are bounded, Atla object
capacity is checked, and custom Atla event origins are initialized from position.
This does not increase population/object counts or expose a public layout API.
A failed whole-floor build still requires staging and recovery before publication.

Native tests cover far geometry, more than 196 candidates, empty-map rejection,
and byte-identical retail output plus next-RNG state. Script inspection also
confirmed that built-in transport routines use fixed road coordinates around
X=3040, including camera, NPC and sound calls. Expanded transport remains
unfinished: a diagnostic retail replay was stopped without a successful result;
its test source and log are retained outside the repository. Full expanded-floor
playthroughs are not validated by the sampler tests.

Validation: Clang 23.1.2 Windows game/test builds pass, with 789 passing
full-suite tests, 34 skips and 1 disabled benchmark. All 29 Python checks pass.
Both 3,840-case no-mod/disabled exports retain their previous hashes and match
pinned upstream except for map memory size. Protected PS2 files are unchanged.
All seven separate PAL retail back-floor scenarios also passed with the
placement wrapper; this does not validate expanded-floor transport.


### Expanded built-in transport scenes

All seven native back-floor entry scenarios now pass on explicit 48-cell maps
using six rooms spread across a 46-cell build area. The shared-manager action
runs the real confirmation and transport script, verifies both front/back
extents, then requires 90 consecutive normal-play frames on a populated back
floor cell, unchanged inventory and all 50 access flags, and retirement of the
owned request. This verifies one first-floor entry per dungeon, not every seed,
maximum room/population settings, return travel, or a complete floor playthrough.

The first visual capture exposed an actual integration bug: the expanded cave
cart travelled through black space because the rail tunnel moved to column 47
while the built-in script still used column 19. An internal adapter now checks
the complete PAL script fingerprint and makes a runtime copy with the reviewed
transport X operands relocated by 4,480 world units. Camera, actor and sound
operands move together; differences between paired X constants stay unchanged.
Only the selected built-in transport program uses that copy. Original buffers,
assets and PS2 files are unchanged, and retail extents bypass inspection/copying.
Moon Sea and Demon Shaft programs require no coordinate operands to change.
The corrected cave tunnel and forest transport scenery were visually inspected.

Unknown/modified scripts and mixed front/back extents are rejected by this
adapter. These cases need an explicit custom-content contract or another
verified adapter; a future public layout operation must preflight these
requirements before publishing a floor. This is not the contributor's cutscene
framework and does not introduce its contracts. Public owned layout settings,
staged failure recovery, higher population/object capacities, maximum-size
retail playthroughs and content/resource validation remain unfinished.

Reproduce the optional expanded scenarios individually with `DC_DATA`, a fresh
`DC_BACK_FLOOR_AUDIT` directory and `DC_EXPANDED_BACK_FLOOR_AUDIT=1`, selecting
`ModBackFloorRetail.ExpandedCaveTransport` (or the other six named scenarios).
They record frame/position diagnostics and, where sampled during the road
segment, a `road.png` capture. The independent local-PAL transport test verifies
all seven script fingerprints, exact changed operand counts, preserved inputs,
and rejection of altered programs. No copyrighted scripts are stored in Git.

Validation: Clang 23.1.2 Windows game/test builds and the full suite pass
(790 passed, 42 skipped, 1 disabled). All 29 Python checks pass. Both
3,840-case empty/disabled seed exports retain their previous hashes and match
pinned upstream except for map memory size. Other platforms remain unverified.
All seven retail and seven expanded PAL transport scenarios were rerun
separately on the final revision and passed.


### Maximum-layout replay and nonpublishing preparation

The seven optional `ModBackFloorRetail.Maximum*Transport` scenarios request
64 rooms, a 46-cell build area and room sides 3�6. Both generated floors must
contain more than 16 rooms and retain the expanded extent. All seven reached
and sustained normal back-floor play. This is maximum requested packing for
one first-floor seed per dungeon; the native packer may place fewer than the
requested 64 rooms. It does not prove every seed, later floor, key route,
return journey, or larger monster/chest populations.

An internal `PrepareMaps` operation now builds the front/back pair and object
placements in owned temporary maps and a copied dungeon status. It validates
runtime event capacity and frames before returning, without drawing or changing
loaded frames. Successful preparation returns the prospective RNG and generator
scratch state alongside the maps/status. Both success and failure restore the
live status/map/event pointers, RNG and all generator scratch arrays. Scratch
snapshots stay local to the process; they are not a save or plugin ABI format.
The scratch copy matters because later native room/door operations can reuse
those arrays. No staging allocations or calls run on the no-mod path.

The maximum-layout replays additionally prepare a valid candidate, reject an
impossible room packing, and reject an event definition without a placement
frame. They compare complete live front/back map bytes, dungeon-status bytes,
event-manager bytes, generator scratch and RNG state before/after, then enter
the back floor normally. Separate tests check invalid-request context restoration
and rejection of a malformed scratch snapshot before any write.

This is preparation, not completed public layout support. The owner-scoped
configuration/transaction API, transport preflight integration, accepted-map
publication at the native loading boundary, recovery/reporting and broader
content/resource validation remain required. The public operation count stays
at 106. The contributor's item, weapon and cutscene contracts remain reserved.

Validation: Clang 23.1.2 Windows game/test builds and the full suite pass
(792 passed, 49 skipped, 1 disabled). All 29 Python checks pass. Both
3,840-case no-mod/disabled exports retain their previous hashes and match pinned
upstream except for map memory size. No PS2 originals changed. Other platforms
remain unverified.
All seven maximum-layout PAL replays passed again on the final revision,
including successful staging and both injected failure cases.


### Public owned layout API

Three operations now configure, inspect and reset per-dungeon layout claims.
Conflicting owners and failed callback transactions cannot publish settings.
Accepted settings apply at future native floor loads; map preparation and
transport preflight precede publication. Rejected builds report a reason and
fall back to retail generation. No claims take the original native path.

All seven PAL first-floor Lua-driven expanded-layout/back-floor replays pass.
An impossible packing request also reports rejection and completes retail
back-floor travel. These cases do not establish every seed, later floor or
key route. Population, chest loot and extra floors remain outstanding.
The contributor item, weapon and cutscene contracts remain reserved.

Validation: Clang 23.1.2 Windows game/test builds pass. Full suite: 795 passed,
57 skipped, one disabled; all 29 Python checks pass. Both 3,840-case empty
and disabled-mod exports match pinned upstream except map memory size.
PS2 source is unchanged. Linux, macOS and Android remain unverified.


### Explicit generated chest targets

Owned layouts accept an optional chests field (2-24). Omission preserves the
retail random target. Native map/crystal assignment and loot selection remain
in use. Actual front/back totals are reported; existing generation chests
consume the same 24 slots. Slot or placement exhaustion rejects staging and
uses retail fallback. Larger chest storage, loot overrides and monster
population controls remain outstanding.

Clang 23.1.2 game/test builds pass: 796 tests passed, 57 skipped, one disabled.
All 29 Python checks and seven first-floor PAL Lua-driven 12-chest/back-floor
replays pass. Both 3,840-case empty/disabled seed exports match pinned upstream
except map memory size. PS2 source is unchanged; other platforms unverified.


### Owned native-slot population requests

Owned layouts accept optional monsters (2-15; Forest minimum 3). The request is stored on the
generated map, survives copying/back-floor entry and clears on initialization.
Only positive random-model placement calls use it; model-specific and zero
counts preserve native behavior. Placement selection, spacing and key assignment
remain native. Seven PAL first-floor replays each verified five monsters on
both front and back floors through a real Lua request and shared-manager action.

This is not expanded monster capacity. Sixteen native slots remain. Reaching
the reference 256 slots requires coordinated CMonstorUnit script, character,
interpreter, effect, sound, projectile and guard arrays; the shared
MonstorScriptBuffer allocator and loading/transition bindings; per-frame loops;
and framework handles/reaction/ownership storage. A blanket replacement of
16 would also alter unrelated per-monster collision/sound arrays.

Validation: Clang 23.1.2 game/test builds; 797 tests passed, 57 skipped,
one disabled; 29 Python checks passed. Both 3,840-case empty/disabled
exports match pinned upstream except map memory size. PS2 source unchanged.
Other platforms and broader seed/key-route coverage remain unverified.


### Population key-carrier minimums

Native SetKey skips assignment with one or fewer monsters; Wise Owl needs
three separate key carriers. The host now rejects one-monster requests and
Forest requests below three. The preparation boundary also rejects the
invalid Forest setting before publishing anything. Minimum Cave (two) and
Forest (three) PAL replays verified the required key carriers and back-floor
travel. All seven managed-layout replays now assert their front-floor keys.
This is first-floor seed evidence, not proof of every placement or route.

Clang 23.1.2 game/test builds pass: 797 passed, 59 skipped, one disabled.
All 29 Python checks pass. Empty/disabled 3,840-case seed exports retain
their hashes and differ from pinned upstream only in map memory size.
PS2 source remains unchanged; other platforms remain unverified.


### Monster capacity contract groundwork

Framework handles, transactional actor values, owned stats/scales, death
bounds and the two-unit reaction queue now share one slot-capacity constant.
Compile-time checks require all thirteen native per-monster arrays to match
it. Per-monster sound/collision dimensions are separate and unchanged.
Capacity is still sixteen: native allocation, lifecycle and active iteration
must be extended together before larger populations can be exposed.
Clang 23.1.2 game/test builds pass, all 44 affected entity/combat/physics
tests pass, and all 29 Python checks pass. No PS2 files changed.


### Dormant expanded monster storage

The generated port header now reserves 256 entries in all thirteen paired
CMonstorUnit arrays. No PS2 source changes. Native and framework active limits
remain sixteen; public population requests still stop at fifteen. A SlotLimit
boundary now protects entity tracking, queries and writes from dormant storage.
Per-monster sound, hit, guard and collision dimensions are unchanged.

This is storage groundwork, not completed 256-monster gameplay. Scripts for
additional actors, lifecycle initialization, active iteration, resource budgets
and high-population playthroughs are still required. A test fills dormant
slots with apparently live actors and verifies retail counting, framework
queries and cleanup ignore them. All 45 affected entity/combat/physics tests,
29 Python checks, seven retail and two managed PAL transport replays pass.
Both 3,840-case seed exports retain their previous hashes; the existing
map-size difference remains the only difference from pinned upstream.

Clang 23.1.2 Windows game/test builds pass. Full suite: 798 passed, 59
skipped, 1 disabled. Other platforms remain unverified.


### On-demand extra monster script arenas

A common loading/transition binder preserves native sixteen-buffer binding.
Explicit internal requests up to 256 allocate only the extra requested arenas
through upstream SetDataBuffer/guarded arena storage, with native 1875-quad
sizing and host headroom. Default binding neither constructs extra descriptors
nor maps their backing storage. The arenas are shared across the active unit,
like retail script buffers, and retain upstream clear/reuse semantics.
All native and port loading/transition binding sites now use the helper.

This does not activate additional monsters. Public population limits remain
unchanged pending lifecycle, iteration and model-resource budget work. Tests
verify every extra buffer through slot 255 is distinct and aligned, invalid
requests publish nothing, retail binding preserves allocation count and
existing cursors, and arena clear/rebinding resets extra cursors without
changing active limits. Seven retail and two managed PAL replays pass.

Clang 23.1.2 builds pass: 800 tests passed, 59 skipped, 1 disabled.
All 29 Python checks pass. Both 3,840-case empty/disabled exports retain
their hashes and match pinned upstream except map memory size. PS2 source
is unchanged; other platforms remain unverified.


### Per-unit monster lifecycle and iteration

Units now default to sixteen active slots with a bounded internal limit up to
256. Native unit counting, visibility, movement, drawing, cleanup and free-slot
search use that limit; sorting scratch has full storage capacity. Inner sound,
hit and collision-shape dimensions remain retail. Framework identities,
transactions and ownership storage cover 256; queries obey the active limit.
Reaction queue iteration retains retail bounds until an expanded unit explicitly
queues work, then returns to retail bounds when drained. Script binding follows
the unit limit. No public operation activates more than sixteen slots yet.

Tests cover slot-255 cleanup/healing, hiding stale high-slot handles on shrink,
two full 256-slot reaction queues, invalid internal bounds and cleanup of all
extra script arenas. All 50 affected tests and 29 Python checks pass; native
lifecycle statement comparison permits only the explicit actor-loop bound
change. Seven retail and two managed PAL transition replays pass. Both
3,840-case seed exports keep their hashes and differ from pinned upstream
only by map memory size. PS2 files remain unchanged.

Remaining before public high populations: load-boundary activation/reset,
external actor consumers (including targeting/rendering/script access),
resource and collision budgets, and high-population playthrough validation.

Clang 23.1.2 Windows game/test builds pass. Full suite: 803 passed, 59
skipped, 1 disabled. Other platforms remain unverified.


### External monster consumers (2026-10-20)

Lock-on scratch and script variables now reserve 256 slots. Target selection,
nearby-monster opcodes, thrown items, projectiles, battle music, detail drawing
and damage-source recording obey the unit active limit. Per-monster hit spheres,
sound slots and the shared sixteen-entry hit-effect pools remain unchanged.
No public API activates expanded populations yet.

Clang 23.1.2 Windows game/test builds pass. Three new tests cover slot-255
targeting, nearby searches and independent script variables; the full suite
has 806 passed, 59 skipped and 1 disabled. All 29 Python checks pass.
Fresh PAL retail Cave and managed Cave/Forest transition replays pass.
Both 3,840-case no-mod/disabled-mod seed comparisons differ from pinned
upstream only in map storage size. PS2 diff remains empty. This does not
prove high-population gameplay or other-platform behavior.

Next gates remain load-boundary activation/reset, bounded collision/resource
storage and high-population gameplay validation before exposing higher counts.


### Expanded monster collision append budget (2026-10-20)

Expanded units now copy the existing terrain results into a separate, lazily
allocated polygon buffer before appending actor collision. The helper validates
the complete shared frame graph and MDT mesh bounds, then reserves the worst-case
space for every active other actor before calling native writers. The budget is
65,536 polygons, 4,096 frame visits and 64 child levels. Unsupported geometry,
cycles and excess budgets throw before native actor writes. Retail units keep
the original work-buffer append path; no public API enables expanded units yet.

Focused tests build 511 polygons from 256 slots, preserve terrain ordering,
exclude the current/dormant actors and reject invalid geometry/graphs/budgets.
This addresses actor appends only: terrain generation still writes into the
original buffer first. Terrain bounds and load-boundary preflight/error handling
must be completed before exposing higher populations. High-population native
combat/playthrough coverage and other platforms remain outstanding.

Validation: Clang 23.1.2 Windows game/test builds pass; full suite 808 passed,
59 skipped, 1 disabled; 29 Python checks pass. Fresh retail and managed Cave
PAL back-floor replays pass. Both 3,840-case seed exports retain their prior
hashes and differ from pinned upstream only by map storage size. PS2 unchanged.


### Bounded procedural terrain for expanded units (2026-10-20)

Expanded monster stepping now bypasses the fixed terrain work buffer as well
as the actor append buffer. The terrain collector validates every selected
frame/mesh and the total polygon budget before native writes. It preserves
the native nine-neighbor order, rotation arithmetic, chest order and the
player-distance filter for Atla collision. Bounds cover both retail and
expanded map coordinates. This helper currently accepts procedural floors;
expanded populations on fixed/custom maps remain unsupported. Retail units
continue through the original terrain collector and work-buffer allocation.

Tests compare native geometry ordering/coordinates at both grid sizes, collect
500 terrain polygons and reject invalid indices/geometry before transforms.
All seven managed PAL dungeon replays plus retail Cave compare loaded native
terrain output at player and active monster positions on front/back floors.
These eight fresh replays pass; full Windows suite: 810 passed, 59 skipped,
1 disabled. Clang 23.1.2 game/test builds and 29 Python checks pass. Both
3,840-case no-mod/disabled-mod exports retain prior hashes and differ from
pinned upstream only in map storage size. PS2 remains unchanged.

Next: load-time collision/resource preflight and failure reporting, activation
and reset at floor boundaries, then high-population combat/playthrough tests.
No public population limit was raised in this change; cross-platform validation
and the other coverage-ledger gaps remain outstanding.


### Floor population preparation and collision preflight (2026-10-20)

Internal PrepareSlotsForFloor now validates requested expanded slots and
collision resources before binding scripts or publishing the active limit.
It checks every floor cell's worst-case nine-cell neighborhood, all 24 chest
and eight Atla contributions, and every other actor, against polygon/frame
budgets. Invalid parts, unsupported geometry and oversized resources fail
before changing the unit. Sixteen-to-sixteen preparation returns immediately.
Shrinking cleans retiring actors/scripts while their slots remain active, then
restores the requested limit and clears retired script pointers.

Tests cover invalid distant cells, aggregate frame budgets, allocation-free
retail behavior, rejection preserving live state, full 256-slot initialization
and cleanup back to sixteen. This is an internal loading helper, not yet
wired to a public expanded-population request. Load failure reporting, native
load/reset integration, remaining resource audits (including audio/collision
identifiers) and high-population gameplay remain necessary.

Clang 23.1.2 Windows game/test builds pass: 812 tests passed, 59 skipped,
1 disabled, plus 29 Python checks. Eight fresh PAL replays (retail Cave and
all seven managed dungeons) pass the 256-slot collision-resource preflight
on front/back floors and native terrain comparisons. This validates capacity
calculations, not 256 populated actors in play. Both 3,840-case seed exports
retain prior hashes, differing from pinned upstream only in map storage size.
PS2 unchanged; other platforms unverified.


### Expanded hit-record storage and resource audit (2026-10-20)

CCollisionData now reserves 8,288 records, with an active limit of 96 by
default. Explicit PrepareForMonsterSlots requests above sixteen enable
96 + slots*32 records (sixteen attack spheres over two ticks plus shared
headroom). Native Set, player/monster queries and hit lifetime iteration
obey that active limit. Floor preparation clears retiring records before
shrinking. Expanded exhaustion throws before overwriting a record; the
retail allocation path remains unchanged. This internal pool preparation
is not yet connected to public higher populations.

Tests fill the full pool, verify slot-255 owner 1475 survives, query the last
record, reject exhaustion without overwrite and clear back to 96. Source
audit found no hardcoded byte-copy consumers of CCollisionData.

The audio audit identified an additional blocker: current_monster*2 reaches
256 at slot 128, but CSound's queued effect messages narrow voice to uint8_t,
aliasing slot 0. The native sequence pool also has only 32 records. These
need an expanded-only allocation/routing solution using official Kazarov;
no private renderer/audio fork or PS2 edits are introduced. Exhaustion
reporting, floor integration and high-population combat validation remain.

Clang 23.1.2 Windows game/test builds pass; 814 tests passed, 59 skipped,
1 disabled; 29 Python checks and three fresh PAL replays (retail Cave,
managed Cave/Forest) pass. Both 3,840-case no-mod/disabled-mod seed exports
retain prior hashes and differ from pinned upstream only in map storage
size. PS2 unchanged; other platforms and high-population play unverified.


### Expanded effect voice routing (2026-10-20)

The port can explicitly enable full-width logical effect IDs for expanded
monster populations. A lazily allocated 256-lease router keys sounds by
port/bank/program/logical voice and sends byte-sized tokens through the
unmodified official Kazarov interface. Repeated play refreshes the lease;
when full, least-recently-played reuse queues a stop before the replacement.
Stale control/stop requests for evicted IDs are ignored. Returning to retail
queues stops for every lease, then restores the original byte-ID path.
Default sixteen-slot preparation neither allocates nor changes routing.
The upstream mixer's 48 physical voices remain unchanged.

Unit tests cover all 256 logical monster IDs, deterministic eviction and stale
lookups. An official-mixer test plays voices 0 and 256 together, stops 256
without stopping 0, ignores stale controls, and stops the final sound on reset.
This internal activation is not yet wired to public high-population requests.
The separate native 32-entry sound-sequence pool, coordinated floor activation
and reset, error reporting and populated gameplay still need completion.

Validation: Clang 23.1.2 Windows game/test builds pass; 816 tests passed,
59 skipped, 1 disabled; 29 Python checks and three fresh PAL replays pass.
Both 3,840-case no-mod/disabled-mod seed exports retain prior hashes, differing
from pinned upstream only by map storage size. PS2 and official Kazarov
sources unchanged. Cross-platform and high-population gameplay unverified.


### Expanded native sound sequences (2026-10-20)

Native sound-sequence storage reserves 288 entries. Lookup, initialization,
refresh, expiry and all-stop loops use an explicit active limit, default 32.
PrepareSoundSequencesForMonsterSlots enables 32 + requested slots only for
expanded units, stopping through the previous limit before resetting the
new range. Retail-to-retail preparation is a no-op. The s16 logical voice
field already accommodates all 256 monster slots without truncation.

Tests fill the retail and expanded pools, reject excess entries, refresh
voice 510 in a full pool, expire every entry through native SndStep, and
retire all high sequences before shrinking. Dormant retail storage is
untouched. The expiry test deliberately uses a rejected audio ID to isolate
sequence lifecycle; the prior official-mixer test covers sound routing.

Coordinated floor loading must stop/reset sequences before clearing voice
leases, activate hit/script/monster limits together, and report failures.
These internal helpers are still not exposed as public higher-population
requests; high-population native gameplay and full coverage remain pending.

Clang 23.1.2 Windows game/test builds pass: 818 tests passed, 59 skipped,
1 disabled; 29 Python checks and three fresh PAL replays pass. Both
3,840-case seed exports retain prior hashes and differ from pinned upstream
only by map storage size. PS2 unchanged; other platforms unverified.


### Coordinated population loading and first populated replay (2026-10-20)

PreparePopulationForFloor coordinates monster/script slots, hit records,
sound sequences and voice routing. Sequence stops resolve through old leases
before routing resets. The native monster-load boundary invokes preparation
after resources load; ordinary maps select sixteen slots. Leaving dungeon
mode through the mod game tick resets an explicitly expanded population.

A fresh PAL test prepares 32 actors with native ArrangementPos on a managed
Cave floor, moves slot 31 near the player, confirms it reaches active state,
runs 180 frames, and invokes the shared-action native back-floor transition.
The normal back floor restores monster=16, hits=96, sequences=32 and disabled
expanded audio routing. A unit test also checks coordinated script/hit/audio
retirement. This is initial native gameplay evidence, not proof of dense
combat, every dungeon or the maximum population.

Public layout requests remain capped at 15 while testing higher counts,
placement limits, error reporting and transition/quit behavior. The replay
injects an internal request; it does not claim the public API exposes 32 yet.
The rest of the API/tooling coverage ledger remains outstanding.

Clang 23.1.2 Windows game/test builds pass: 819 tests passed, 60 skipped,
1 disabled; 29 Python checks pass. Fresh retail Cave, managed Cave/Forest
and the populated Cave transition pass. Both 3,840-case seed exports retain
prior hashes and differ from pinned upstream only by map storage size.
PS2 unchanged; other platforms unverified.


### Maximum native population replays (2026-10-20)

The 129-actor Cave replay passes, including activation of slot 128. The first
256-actor replay exposed native placement saturation: only 252 were placed.
Explicit expanded requests above 192 now permit fewer than four neighbors
within the native 480-unit radius instead of fewer than three. Requests at
retail active limits keep the original rule regardless of count; minimum
25-unit spacing and terrain/chest/Atla checks remain native.

Fresh 256-actor replays now pass for all seven procedural dungeons. Each
confirms all 256 actors were placed, moves slot 255 near the player, observes
its active state, runs 180 frames and uses the shared-action native back-floor
transition to restore retail monster/hit/sequence/audio limits. These are
internal test requests on 64-room floors, not arbitrary-layout or sustained
combat evidence. Smaller layouts can still fail placement and need explicit
reporting. Public requests remain capped at fifteen until that reporting
and quit/reset paths are ready. The broader coverage ledger is unfinished.

Clang 23.1.2 Windows game/test builds pass: 820 tests passed, 68 skipped,
1 disabled; 29 Python checks pass. The seven maximum-population replays,
129-actor Cave replay, retail Cave and managed Forest checks pass. Both
3,840-case no-mod/disabled-mod seed exports retain prior hashes and differ
from pinned upstream only in map storage size. PS2 unchanged; other
platforms remain unverified.


### Expanded-population quit and exception cleanup (2026-10-20)

RunGame now finishes any explicitly expanded population on normal return
or a propagating C++ exception, before game/audio resources can be reused.
It restores retail monster/hit/sequence limits, clears retired script
pointers and submits queued sound stops. The helper returns immediately
when no expanded population exists, preserving the default run path.

Fresh PAL tests place all 256 monsters, activate slot 255, run 180 frames
and either request a direct quit or throw a deliberate game-pump exception.
Both confirm retail resource limits and cleared high slots after RunGame
returns/throws. The exception test also verifies the original error survives.
These tests do not represent process termination or allocation failure.

Placement-failure reporting and public higher-population requests remain
next; the broader API/tooling coverage ledger is still incomplete.

Clang 23.1.2 Windows game/test builds pass: 820 tests passed, 70 skipped,
1 disabled; 29 Python checks pass. Direct quit, exception exit, retail Cave
and maximum-population transition PAL replays pass. Both 3,840-case seed
exports retain prior hashes and differ from pinned upstream only by map
storage size. PS2 unchanged; other platforms unverified.


### Native population outcome reporting (2026-10-20)

Owned layout readback now separates map-generation success from native
monster placement. last_build.population_front and population_back report
requested, placed, state (applied/partial) and reason after their placement
calls. The hook records the change in native alive_count rather than
assuming all attempts succeeded. It only publishes for the matching applied
claim, runtime epoch, explicit population setting and current floor.
Default maps have no reporting call; missing fields mean not yet reported.

A fresh PAL test fills all active slots, attempts more native placement and
verifies partial/zero-added with an explanation. Normal managed front/back
placement reports applied with the actual count; the back-floor transition
remains usable after the partial result. MOD_HOST_API documents the fields.
Public counts remain capped at fifteen pending high-request preflight
failure fallback and public-path testing. Full coverage remains unfinished.

Clang 23.1.2 Windows game/test builds pass: 820 tests passed, 71 skipped,
1 disabled; 29 Python checks pass. Partial-placement, retail Cave, managed
Forest and maximum-population transition PAL replays pass. Both 3,840-case
seed exports retain prior hashes and differ from pinned upstream only by
map storage size. PS2 unchanged; other platforms unverified.


### Expanded population preflight fallback (2026-10-20)

The loaded-floor population boundary now catches read-only collision-resource
preflight rejection, records the reason for a matching applied claim, clears
that map's population override and restores retail monster/hit/sequence/audio
limits. Allocation and native setup exceptions still propagate. Rejected
reports use placed=null and fallback=native; they do not claim the fallback
placement count. Public population limits remain fifteen pending public-path
validation.

A focused test starts with expanded resources, rejects a 256-slot request on
the polygon budget and confirms native front/back counts and resource limits
are restored. Retail-to-retail setup preserves existing actor state. Clang
23.1.2 game/test builds pass: 821 passed, 71 skipped, one disabled. All 29
Python tests and the PAL partial-placement/back-floor replay pass. Both
3,840-case seed exports retain their previous hashes and differ from pinned
upstream only by map storage size. No PS2 changes; other platforms remain
unverified.


### Public expanded monster populations (2026-10-20)

The public dungeon.layout_set monsters option now accepts 2..256 (Forest
minimum three), backed by the loaded-floor resource coordinator and preflight
fallback. Native explicit-model and zero-count placement calls remain unchanged.
Docs describe opt-in density, resource lifetime and actual placement reports.

Seven fresh PAL replays request 256 through a real Lua package, with no internal
population injection. All seven procedural dungeons place 256 on both front and
back floors, report actual counts, preserve required front-floor key carriers,
complete the shared-action native back-floor transition and restore retail
limits/script pointers on exit. These are 64-room scenarios, not proof that
all layouts can fit 256 or sustained-combat coverage. Partial placement remains
reported rather than guaranteed away.

Clang 23.1.2 Windows game/test builds pass: 821 passed, 78 skipped, one disabled.
All 29 Python tests pass. Both 3,840-case no-mod/disabled-mod exports retain
their prior hashes and differ from pinned upstream only in map storage size.
PS2 unchanged; Linux/macOS/Android and broader capability parity remain open.


### Reproducible retail smoke runner (2026-10-20)

Added tools/chronicle_mod_smoke.py for nineteen built-in PAL scenarios: seven
retail back-floor transitions, seven public maximum populations and five
population lifecycle/rejection checks. Each runs in a fresh process and save
profile, through the existing shared menu-action tests. The runner records
Clang CMake metadata, executable hash, XML, logs and incremental JSON results.
Only the matching completed test can pass; skipped/missing/wrong/empty results,
crashes, launch errors and deadlines fail. Inherited test/game overrides are
cleared. Existing output directories, output within game data and PS2 paths
are rejected. No retail assets or generated profiles are distributed.

All nineteen scenarios passed through the tool on the existing Clang 23.1.2
Windows build. All 35 Python checks pass, including new isolation, environment,
compiler, output, timeout, launch-error and result-classification cases. This
change is tooling/docs only and does not change the game binary or no-mod path.
PS2 unchanged. Other platforms, arbitrary package certification and broader
API/content-tool coverage remain unfinished.


### Owned weighted chest loot (2026-10-20)

dungeon.chest_loot_get/set/reset add weighted retail item pools per dungeon,
front/back selector and small/weapon chest kind. One owner holds each selector;
claims participate in callback rollback and retire with owner/runtime lifetime.
They apply only to future random chest generation. Item kinds, payload needs,
unique IDs, list lengths and weights are validated. Active claims consume one
weighted RNG draw; absent claims return directly to the existing port selection
without consuming RNG. Native map/crystal guarantees, separately placed door
keys, chest types, traps and placement remain in their existing paths. No new
PS2 overlays or PS2 source edits are needed: the hook is in the existing port
chest-selection override.

Two focused tests cover native selector calls, deterministic weighting, strict
validation, disjoint ownership/conflicts, rollback and retirement. Seven fresh
PAL Lua-package scenarios cover both floors of every procedural dungeon and
preserve map/crystal/key chests. The smoke runner now exposes a chests suite
and defaults to 26 scenarios. Docs cover all three operations and limitations.

Clang 23.1.2 game/test builds pass. The full CTest run plus the corrected generic
query-fixture rerun has 823 passing tests, 85 skipped and one disabled. All 35
Python checks pass. Both 3,840-case seed exports retain their prior hashes and
differ from pinned upstream only by map storage size. Other platforms, larger
chest capacity and the remaining coverage ledger are still open.


### Saved Georama satisfaction readback (2026-10-20)

georama.satisfaction_get reports bounded saved request completion, individual
request flags and the rounded completion percentage for the five settlements.
The result explicitly states its derivation and does not claim to recompute
placement conditions or trigger rewards. Invalid request counts are errors.
This matches the derived readback in pinned MoonMod GetGeoramaSatisfaction;
plot and placement details already exist in georama.get.

Source inspection of pinned MoonMod SetDungeonFloorRules confirms its Chronicle
implementation writes limited-zone table/current fields, which our existing
transactional floor_rules APIs already cover with explicit live application.
The broader catalog description alone does not establish additional seal/key
behavior. Other floor controls, including exit-room rules, remain separate work.

Clang 23.1.2 Windows game/test builds and all 13 focused API tests pass. The
full suite has 824 passes, 85 skips and one disabled test; all 35 Python checks
pass. The new read test checks detached results, rounding, zero requests, invalid
counts/selectors, exact save-byte preservation and unchanged RNG. No automatic
engine work or generation hook was added; PS2 is unchanged. Other platforms and
the broader ledger remain unverified/incomplete.


### Transactional Georama request completion (2026-10-20)

georama.request_set changes an existing saved request's completion flag with
strict boolean/index/count validation. When the selected town is loaded, its
request cache participates in the same staged transaction; mismatched loaded
and saved request counts reject the write. Other towns change only their saved
flag. Host calls require stable play outside menus/transitions. No reward script,
placement condition or content registry is invoked, and no unloaded/default
engine path receives changes. These explicit save writes persist until another
write or native reevaluation; owner retirement does not undo them.

Unit tests cover complete/reset, exact unrelated-byte preservation, cross-command
rollback, invalid/undefined requests, loaded-count mismatch, another town's
isolation and unchanged RNG. A fresh PAL town replay invokes Lua through shared
menu actions, toggles a request, checks saved and native cache values, performs
a native cache-to-save round trip and restores the original completion. The
replay is available as ModGeoramaRetail.SharedActionsUpdateRequestCacheAndSurviveNativeSave
with DC_DATA and a fresh DC_GEORAMA_REQUEST_AUDIT folder.

Clang 23.1.2 Windows game/test builds pass. Final CTest: 826 passed, 86 skipped,
one disabled; all 35 Python checks pass. The PAL replay also passes after the
final stable-play guard. PS2 unchanged. Georama placement/Atla, reward-condition
evaluation, other platforms and the broader coverage ledger remain open.


### Native Atla inspection and atomic collection (2026-10-20)

georama.atla_list exposes available current-floor spheres, native registry
identity/reference data and part/chip contents. georama.atla_collect requires
Toan and stable dungeon play, with collection as the callback's only engine
write. Native grants and registry bookkeeping run on a save copy; only success
publishes save and sphere/map-event retirement together. No global UserStatus
redirection is used. Full chip inventory, rejected grants and invalid registry
or floor membership preserve both sides. Demon Shaft never indexes the native
six-row registry. No custom Atla definitions or pickup animation are introduced.

Four tests cover detached reads and byte/RNG preservation, native part grants
and registry retirement, rejection/mixed-write handling, full chip inventory
rollback and successful chip grant. A fresh PAL Lua shared-menu-action replay
collects a sphere, verifies its disappearance next callback and completes the
native back-floor transition. The full Clang 23.1.2 suite passes: 830 passed,
87 skipped and one disabled; all 35 Python checks pass. No automatic engine
path was changed and PS2 remains untouched. Other platforms, broader Atla
scenarios and the remaining capability ledger remain open.


### Loaded retail Georama placement (2026-10-20)

georama.placement_state, georama.place and georama.remove expose the native
loaded-town placement pipeline through the shared Lua/native host boundary.
Edits require stable walking, available built stock, matching town, and sole
engine-write ownership for the callback. Native fit/dependency rules, bounds,
visibility, request evaluation and town save caches are used. Capacity preflight
prevents native stock increments on a full placement table and overflow of
request evaluation scratch storage. Native fit checking rotates the source
template even on rejection; a scoped restore preserves template transforms.
No new automatic engine hook or PS2 edit is required. Slots are current scene
indices, not durable identifiers; edits persist rather than reverting on disable.

Three focused tests check rejection/save/stock/geometry/template/RNG preservation,
read detachment and exclusive writes. A fresh PAL Norune replay provisions one
built part in a disposable fixture, invokes both operations through Lua shared
menu actions, observes placement/removal and verifies native save serialization
after each. Other towns, road/river/bridge combinations, custom definitions and
completion reward services remain outside this gameplay proof.

Clang 23.1.2 builds both executables. Full regression: 833 passed, 88 skipped,
one disabled; all 35 Python checks pass. The explicit PAL placement replay passes.
PS2 remains unchanged against the framework baseline.


### Georama reference boundary and dungeon camera inspection (2026-10-20)

Rechecked the pinned reference patch: separate georama.condition_set,
request_known_set and river_set are Chronicle Two bindings. Its Chronicle
capability response explicitly reports no separate saved analysis-condition or
river-grid state. Do not mistake these catalog names for missing Chronicle
bindings. Custom pieces and native condition/reward services remain separate
work in this ledger; this audit does not implement them.

The confirmed Chronicle render.camera_get binding is now covered by an on-demand
read of the active native dungeon camera. It returns detached eye/target, pose
angles, orbit distance/height/target angle and follow status; invalid scene or
non-finite native data rejects. Tests preserve native bytes and RNG and check
invalid state/arguments. A PAL shared-action replay reads the pose, modifies its
Lua snapshot, verifies a fresh read is unaffected and completes back-floor travel.
Camera writes remain open: lifecycle identity and conditional owner cleanup must
be verified before exposing orbit mutation. No default engine path or PS2 source
changes are needed for inspection.

Validation: Clang 23.1.2 game/tests build; 835 regression tests passed, 89 skipped,
one disabled; all 35 Python checks pass, plus the explicit PAL camera replay.


### Camera orbit ownership foundation (2026-10-20)

CameraOrbit now stages bounded distance/height/angle edits before touching native
values, with per-field package owners, conflict diagnostics, conditional baseline
restoration, reset and explicit scene discard. It holds no native pointer. Three
focused tests cover conflicting multi-command rollback, independent owners,
repeated edits preserving the original baseline, native changes surviving owner
release, dormant/RNG behavior and scene discard. Clang builds the game and tests.

This is internal groundwork, not a public write API: the operation count remains
120. GameInit reinitializes both global cameras and NowCamera__3; btitem.cpp also
copies MainCamera into SubCamera and temporarily switches the active pointer.
Pointer identity alone cannot distinguish a new scene, and copies can propagate
modded values. The live adapter still needs verified reset/transition/copy handling
and gameplay replays before camera_set/reset can be registered. No automatic
engine hook has been added by this foundation.

Full regression: 838 passed, 89 skipped, one disabled. PS2 remains unchanged.


### Live primary dungeon camera writes (2026-10-20)

render.camera_set/reset now connect CameraOrbit to the primary native follow
camera during stable dungeon play. The getter reports writable/reason/owners.
Camera batches are separate from other engine writes; bounds and per-field
conflicts reject before publication. Values are written once, not forced each
frame: native collision/input/interpolation can supersede them, and cleanup
preserves newer native values. Changes are transient, not saved.

Existing port mode initialization and the GameInit trampoline release before
reinitialization. Existing item-camera copies and the gate-key takeover release
before native use. The game loop retires owners before native processing and
after callbacks; run exit/exception cleanup releases claims. With no claims,
these calls return before reading native state. No PS2 source or renderer changes.

A focused live-adapter test verifies stopped-owner retirement, mixed-write
rejection, copy-boundary restoration and new-scene preservation. The PAL shared
menu-action replay observes exact values immediately after commit, then verifies
conditional reset against values after native camera adjustment and completes
back-floor travel. Full item-effect animation and other-platform camera replays
remain unverified; source boundary inspection and focused copy checks are not
a substitute for those gameplay scenarios. The operation count is now 122.

Validation: Clang 23.1.2 builds; 839 passed, 90 skipped, one disabled;
35 Python tests and the explicit PAL orbit replay passed. PS2 unchanged.


### Native dungeon lighting inspection (2026-10-20)

dungeon.lighting_get exposes the exact native front/back fog rates/colors,
background, ambient and three directional-light vectors/colors, plus the active
lighting side and current map draw-distance scale. Direction columns and color
rows are converted to per-light arrays without modifying or normalizing native
state. Missing maps, wrong mode and non-finite values reject. The read is
on-demand and adds no automatic engine hook.

Focused tests verify both sides, detached data and RNG preservation, rejection
and asymmetric native matrix mapping. An explicit PAL shared-menu-action replay
reads actual floor values, verifies Lua mutation cannot alter native lighting,
and completes back-floor travel. This covers lighting inspection only. Owned
lighting writes, floor-load cleanup, view-scale mutation and rendered write
verification remain open. The public operation count is 123.

Clang 23.1.2 validation: 841 passed, 91 skipped, one disabled; all 35 Python
checks and the explicit PAL lighting replay passed. PS2 unchanged.


### Detached lighting ownership transactions (2026-10-20)

LightingControl stages front/back fog, background, ambient, each directional
light's direction/color, and current-map view_scale on value snapshots. It
validates finite bounds, RGB bytes, unique zero-based light indices, nonzero
normalized directions and ordered fog ranges, including native float rounding.
Fog is one ownership group so partial edits cannot split its coupled range;
RGB and direction vectors are also whole groups. Front/back and disjoint groups
can have independent owners. Conflicts and late failures leave values and
claims unchanged. Reset/retirement restores only groups still matching the last
actual stored float values, preserving later native changes.

Four focused tests cover conflict/late-error rollback across sides, precision
collapse, normalization, float readback/restoration, unrelated writes not
adopting newer native values, invalid inputs and dormant byte/RNG preservation.
This is internal groundwork, not a public lighting write API; count stays 123.

Native TEIGIMdsLoad in ps2/src/itemdata.cpp writes the lighting groups and
VIEWLEVEL during model-definition loading, including reuse loads. The live
adapter must release before this boundary and before native camera/game scene
initialization, handle map identity for view_scale, and avoid throwing from
cleanup. Only after load/retirement tests and a rendered gameplay replay should
lighting_set/reset be registered. PS2 remains read-only.

Clang 23.1.2 game/tests build and full regression pass: 845 passed, 91 skipped,
one disabled; 35 Python checks pass. No live lighting hook was added.


### Live owned lighting and view scale (2026-10-20)

dungeon.lighting_set/reset now bind the detached controller to native front/back
lighting globals and the active native map's draw scale. The getter reports
writable/reason/owners. All batch validation and allocation finish before native
publication; writes cannot mix with other engine domains. Group ownership and
conditional restoration preserve later native changes. Public operation count 125.

Checked port-generated overlay hooks release before GameInit, TEIGIMdsLoad,
bound-map initialization and native view-scale reset. Existing port scene/run
boundaries and pre/post callback checks retire owners. The native map objects
have process lifetime; the adapter accepts only those two live maps. A constant-
initialized inactive flag guards early global constructors and all dormant paths.
Cleanup never unwinds through native loading; invalid snapshots discard claims
without partial restoration. Original PS2 inputs are unchanged.

Focused tests cover map identity, owner retirement, new-scene preservation and
transaction invariants. A fresh PAL shared-menu-action replay sets both fog
sides/background and view scale, verifies values/owners, measures a large red
region in the rendered frame, resets and verifies the red region disappears,
then completes native back-floor travel. Captured apply/reset frames were also
visually inspected. The replay waits for initial fade completion and uses native
fog factor 0 for full fog; factor 255 retains scene color. Other lighting groups,
all floors and other platforms still need broader rendered gameplay coverage.

Clang 23.1.2 builds; 846 tests passed, 92 skipped, one disabled; 35 Python checks
passed. Regenerated 3840 seed cases: empty and disabled packages have no non-size
difference from pinned upstream 50eaff63; only map_bytes differs 142512 vs 76576.
This is generator evidence, not proof of all no-mod gameplay on every platform.


### Author scene-validation suites (2026-10-20)

chronicle_mod_smoke.py now runs mixed native test suites, selecting the matching
fresh Georama request/placement profile variable instead of assuming every test
uses the back-floor profile. New georama and rendering suites cover seven
shared-menu scenarios: request cache, placement/removal, Atla collection, camera
read/write and lighting read/rendered write/reset. Default coverage is 33 scenarios.
Each result identifies its profile variable and relative PNG evidence paths.
Repeated suite selection remains deduplicated and inherited audit/sharding
variables are removed. No user mod directory or personal save is used.

All seven new scenarios passed through the author runner with Clang 23.1.2 build
metadata, and all 36 Python checks pass. The mixed-suite test verifies profile
isolation, deduplication and retained rendering artifacts. Existing executable
behavior was unchanged in this tooling-only update; no C++ rebuild was needed.
The public host operation count remains 125.

Audio audit for subsequent work: the pinned official Kazarov exposes byte-based
LoadPcmTrack (WAV/FLAC/Ogg Vorbis), PcmTrack, StreamChannel and Mixer stream-channel
registration/removal. Chronicle sound.cpp uses audio::DefaultMixer. The pinned
MoonMod reference uses those stream facilities for owned custom sounds and a
separate sequence replacement map for music. No private audio backend is implied
by these capabilities. Registration/decode budgets, asset ownership, callback
transactions, audio-thread lifetime and disable cleanup still need implementation
and verification; this audit does not expose audio operations.


### Owned audio stream foundation (2026-10-20)

AudioStreams owns immutable copies of decoded stereo PCM and registers channels
with the pinned official Kazarov mixer only on explicit playback. Local sound
IDs and handles are owner-scoped. Registration enforces 16 sounds per owner,
64 globally, 32 MiB per track, 64 MiB per owner and 128 MiB globally. Playback
enforces 16 voices per owner and 64 globally, finite volume/pan, whole-track
looping and equal-power pan. Stopping, owner retirement, pruning and destruction
remove channels under the mixer lock before freeing them. Clear does not reuse
old voice handles. Construction alone does not register a channel.

Six Clang tests verify rendered PCM after caller mutation, pan and end-of-track
pruning, owner isolation, validation/count limits, stale handles, unchanged
existing-stream samples with a dormant owner, and concurrent rendering during
100 stop/retire/destruction cycles. This is a local mixer test, not a device
listening test or a race-detector run.

This foundation is not yet connected to package loading, Lua/native host calls
or menu actions. Public operation count remains 125. Bounded compressed decoding,
callback transactions, live lifecycle cleanup, music replacement and public audio
services remain outstanding; post-decode registration limits alone do not bound
decoder allocations. No global mixer or retail playback hooks were changed.


### Bounded mod audio decoding (2026-10-20)

DecodeAudio reuses the pinned official Kazarov decoder symbols and staged
headers, without changing Kazarov or retail LoadPcmTrack. WAV, FLAC and Ogg
Vorbis decode in 1024-frame chunks; mono duplicates and multichannel input uses
the front pair. Input is capped at 32 MiB, output at a caller-selected budget
up to 32 MiB, and source formats at 8..192 kHz and 1..8 channels. WAV/FLAC use
an 8 MiB accounted scratch allocator; Vorbis uses an 8 MiB fixed scratch arena.
Output capacity is reserved within its separate budget before decoding.
Declared lossless frame counts are checked before output allocation and against
actual output where available. Rejection returns no partial track.

Four tests cover mono/stereo/multichannel WAV, generated FLAC/Vorbis tones,
exact and exceeded output budgets, invalid inputs, truncated FLAC and a forged
WAV data length. dr_wav clamps that forged length to real input; this loader
does not promise strict container validation. Fixtures contain generated tones
only. No external encoder is required to build or run the tests.

This decoder and AudioStreams remain internal foundations. Package asset
snapshots, aggregate decode admission, host-call transactions, shared-menu
actions and live cleanup still need integration. Public operation count stays
125. No default retail playback path calls this decoder.

Clang 23.1.2 builds passed; 856 native tests passed, 92 skipped and one
disabled. PS2 diff remains empty. Device playback and non-Windows verification
remain outstanding.


### Package audio admission and tooling (2026-10-20)

The optional audio manifest map declares up to 16 contained WAV/FLAC/Ogg files,
32 MiB each and 64 MiB encoded per package. IDs are at most 64 ASCII characters
with the existing lowercase package-ID syntax. Manager and author validation
agree; packing and installation retain declared audio files. Existing ordinary
resources keep their 64 KiB limit. Packages remain default-disabled.

AudioStreams::LoadPackage stages a complete decoded snapshot and publishes only
after every asset succeeds. It rechecks containment/current file size, rejects
read failures or growth, enforces remaining global/owner PCM and count budgets
before decoding, and skips inactive packages without file access. Existing owner
registration is rejected rather than replaced. Failed loading leaves other
registered assets untouched. Snapshot playback survives source removal.

The loader is not yet called from live startup, and no public audio host calls
are advertised. Those integrations, transactions and lifecycle retirement are
still required. Public operation count remains 125.

Clang 23.1.2 builds and 858 native tests passed, with 92 skipped and one
disabled. All 37 Python tests passed, including packing/installing declared
audio. No PS2 changes.


### Shared runtime audio host calls (2026-10-20)

Public operations now total 128: audio.state, audio.play and audio.stop use the
same permission/owner boundary for Lua and native plugins. Active code packages
with declared audio require resources.read and load a bounded snapshot before
initialization; failed packages retire their registrations. No playback occurs
at startup. Audio writes are deferred until callback success and require one
audio command, no money delta and no other engine writes. State reads expose only
the caller's assets/voices. Handles include an instance prefix to avoid reuse
after runtime/save replacement.

Runtime ticks retire failed owners and stop voices on gameplay leave and
mode/map/floor changes; explicit game mode/run entry/exit/exception boundaries
stop voices too. With no audio assets the runtime creates no AudioStreams or
default-mixer dependency. Existing shared-menu actions can request playback.
Tests exercise Lua menu actions against actual default-mixer samples, stop and
scene retirement, failed-callback rollback, mixed-write rejection and stale
handles across instance replacement. Native uses the common dispatch but a
dedicated native-plugin audio replay and retail/device listening are still
outstanding, alongside music replacement and broader audio controls.

Validation: Clang 23.1.2 builds; 861 native tests passed, 92 skipped, one
disabled; 37 Python checks passed. Fresh 3840-case empty/disabled seed comparison
against 50eaff63 again differs only in map_bytes (142512 versus 76576), with no
non-size difference. This does not prove all-gameplay equivalence. PS2 unchanged.


### Native audio replay and remaining reference scope (2026-10-20)

An ABI 2 plugin fixture now invokes audio.state/play/stop from its registered
shared-menu action. Actual default-mixer samples prove deferred playback, stop
and removal on runtime unload. A failing native action proves both queued sound
and owned action-count state roll back. The fixture's Lua bootstrap sets only
the test mode; all audio calls come through the native C ABI. All 42 relevant
native-plugin/audio/runtime tests pass with Clang 23.1.2, including existing
menu lifecycle and host-call budget cases. No engine behavior changed in this
validation update, and PS2 remains unchanged. Device listening and other
platforms still need verification.

Reinspection of the pinned MoonMod Chronicle patch identifies the remaining
audio comparison targets as retail SE playback, retail BGM play/stop, and owned
sequence music set/clear. Its custom sound registration/play/stop is covered
by our declarative assets plus playback APIs. Seek/pause/gain updates are not
required by that pinned audio implementation and are not a parity blocker.
Public operation count remains 128.


### Retail sound table preflight (2026-10-20)

The retail SE audit found variable-length chapter/voice tables behind native
GetSeInfo's broad index ranges. Basic tables have 199 entries despite IDs
100..299; chapter 3 has 44 entries and voice 6 has seven, including terminators.
The port-only ResolveRetailEffect preflight now selects known bounded spans,
rejects out-of-range selectors, holes and terminators, and returns a detached
row. Null chapter/voice tables retain the native fixed-table fallback. It accepts
Chronicle's actual 0..2799 ID range, not the reference API's broader 0..4095.
No native lookup, retail playback or PS2 source is changed.

Clang 23.1.2 builds and two focused native tests pass: short-table tails and
invalid selectors reject; valid rows copy without table or RNG mutation. All
38 Python checks pass, including a read-only source comparison that guards
every copied table bound against the retail declarations. Public operation
count stays 128. Loaded-bank/sample readiness, queued SE dispatch and retail
playback evidence still need implementation before exposing audio.se_play.
The BGM audit also found that SndBgmLoad returns zero for both an already-loaded
set and a failed load; a successful mod result cannot rely on that return alone.


### Retail effect bank readiness (2026-10-20)

RetailEffectReady follows the existing effect-port mapping to the actually
loaded port bank and resolves default-volume rows using the driver's SE table.
RetailEffectHasSamples mirrors the official mixer's key-first, split-index
fallback with its eight-layer limit and requires decoded PCM. Missing banks,
programs, invalid values and unavailable default-volume rows reject. Both are
read-only and called only explicitly; no retail dispatch or default playback
path was changed. Tests use decoded retail-format synthetic HD/BD samples to
verify key and split fallback plus missing/invalid rejection. The focused Clang
audio suite passed eight cases, with the disc-data case skipped.

This closes loaded-bank preflight groundwork, not the public SE operation.
Queued dispatch, shared-menu retail replay and BGM/music replacement remain
outstanding. Public operation count remains 128; PS2 is unchanged.


### Explicit retail effect dispatch foundation (2026-10-20)

QueueRetailEffect validates loaded samples/default-volume rows before touching
the normal driver queue, caps admission at 4096 messages and reserves room for
program/volume/key-on plus possible expanded-voice eviction messages. It then
uses the same CSound SE_Play route and voice zero as native SndSePlay, including
native unmapped-row full-volume behavior. It does not execute during startup
or change any existing retail call site.

A generated sound pack loads a synthetic HD/BD bank through the actual full-
pointer LoadSoundFileFromPack path. The test proves silence before driver Step,
nonzero rendered output and active voices afterward, missing-program rejection,
and native stop cleanup. The legacy 32-bit pointer loader is deliberately not
used: Windows places the test image above 4 GiB. Clang 23.1.2 builds pass; nine
focused retail/monster audio tests pass and one disc-data test is skipped.
PS2 diff is empty. Public API dispatch and shared-menu gameplay validation
remain next; the public operation count stays 128.


### Retail SE host operation (2026-10-20)

The public audio.se_play operation adds Chronicle IDs 0..2799 and optional
volume 0..127 behind game.write. It requires one owned SE command with no other
engine writes or currency delta. The live game bridge requires stable town or
dungeon gameplay, no transition and no pending sound/voice load. Bounded table
lookup and loaded-bank/sample validation precede queue publication. The runtime
custom-audio interceptor now handles only audio.play/stop, forwarding retail SE
to the game bridge even when the same package has custom audio.

The shared-menu runtime test uses a synthetic retail sound pack and detached
table fixture, proves one commit reaches the retail bridge with custom audio
loaded, no voice starts before native Step, and PCM renders afterward. This is
not a PAL gameplay/device listening replay; that live readiness-path validation
is still outstanding. BGM play/stop and music replacement remain unimplemented.
Public operation count is 129.

Clang 23.1.2 builds passed; 868 native tests passed, 92 skipped and one
disabled; 38 Python checks passed. Fresh 3840-case empty/disabled comparisons
against pinned upstream 50eaff63 again show only map allocation size differences,
not a non-size generation difference. PS2 is unchanged.


### PAL retail sound shared-menu replay (2026-10-20)

A fresh PAL Divine Beast Cave replay commits audio.se_play for native SE 1 at
volume 100 through the shared-manager action, runtime transaction and live game
bridge. The mod then waits six callbacks and completes native back-floor travel
without diagnostics. No table, bank or sound sample fixture is substituted in
this replay. It verifies loaded-bank readiness and dispatch in gameplay; the
separate driver test proves rendered PCM timing. This is not physical-device
listening evidence and does not cover every scene or SE ID.

The author smoke runner now exposes --suite audio, taking its default scenario
count to 34. The new real PAL scenario passed with Clang 23.1.2 and a fresh
isolated profile; all 38 Python checks passed. Report, binary hash, compiler
metadata, process log and road image are in local issue evidence under
se-retail-f58f5317d6b34b4d89f76ad3b10d1235. PS2 remains unchanged.
BGM controls and owned sequence music replacement remain the next audio
coverage gaps; public operation count remains 129.


### Native BGM state and stop (2026-10-20)

Public operation count is 131 with audio.bgm_state and audio.bgm_stop. The
read query inspects native set/play/pause/disable/loading state and actual
bank/decoded-sequence readiness, including registered description validation.
It copies values without changing the native driver. The write requires one
owned command with no other engine writes/currency, stable gameplay and no
pending BGM load. It invokes native SndBgmStop, preserving native core-zero
voice-release semantics and retaining the loaded set. No mod means no call.

Focused tests cover detached reads/RNG preservation, mixed-write and pending-
load rejection, and native idempotent stop. The PAL smoke scenario inspects
loaded/playing BGM, stops it through the shared action and verifies the stopped
state before native back-floor travel. The audio author suite now contains two
scenarios and default smoke coverage totals 35. BGM set loading/play preflight
and owned sequence replacement remain outstanding.

Validation: Clang 23.1.2 builds; 869 native tests passed, 94 skipped and
one disabled; 38 Python checks passed. Both audio PAL smoke scenarios passed
separately in fresh profiles. Evidence: bgm-retail-9eed7f97f2f2492eb7e3828c15ba2fc5.
PS2 diff remains empty. This does not establish physical-device listening or
all-scene/platform coverage.


### Detached BGM archive preflight (2026-10-20)

Added an internal staging loader for native music archives. It checks member
ranges, duplicate names, list/config sizes and required bank/sequence members,
then decodes records in native order into owned objects. Short placeholder lines
are ignored as in the native loader; ambient-bank records are preserved. No
live audio state, native buffers or mixer ports are touched. This helper has no
production caller yet; public operation count remains 131.

The supplied PAL assets contain 57 archives: 55 preflight successfully; sets
120 and 130 intentionally fail with a missing-sequence diagnostic because their
lists name d03a_a.sq/d04a_a.sq while the archives contain different names. The
preflight does not guess replacements or alter the vanilla loader. Synthetic
coverage includes owned decoded data, RNG preservation, malformed ranges,
duplicate/missing members, broken sequences and oversized configuration.

Still required before exposing BGM play/load: validate registered track metadata
and configuration, publish staged state safely, and test failure preservation
and playback through the shared menu action. Archive size limits do not yet
constitute a separate decoded-bank memory budget. Owned music replacement also
remains outstanding.


### Restart a loaded native BGM track (2026-10-20)

Added audio.bgm_play with strict track-only arguments, sole owned commit,
stable-gameplay gating and readiness validation before native stop/play. It
checks track zero too because native default-volume lookup dereferences that
track regardless of the selected track. Disabled music and pending loads are
rejected. The native driver has no new default call site.

The shared-menu PAL replay now covers stop and restart followed by back-floor
travel. All three audio scenarios passed, along with 20 focused native tests
and 38 Python tests, using Clang 23.1.2. Author audio suite has three scenarios
(36 default scenarios). Public operation count is 132. Loading another BGM set
and owned sequence replacement are still incomplete; this operation selects
only a currently loaded track. Physical-device listening and other platforms
remain unverified.

PAL report: bgm-play-2f26ba6bbebd40f3b112ced9810831e0/report.json in local
framework expansion evidence. Full Clang regression completed without failures;
PS2 diff against the Chronicle baseline is empty.


### Detached BGM configuration validation (2026-10-20)

Music preflight now parses REVERBE and TABLE integer records without running
the native script interpreter or changing its global SoundInfo/se_list state.
It preserves record order (last REVERBE wins), ignores TABLE values as native
SetBGMFile does, and clamps mode/depth to the official mixer's 0-9/0-127 limits.
Absent configuration leaves reverb unspecified; present empty configuration
stages the native zero defaults. Malformed/unknown commands, integer overflow,
trailing junk and data after a NUL terminator reject the request. Only whitespace
and comma-separated integer command syntax is supported by this preflight.

Clang 23.1.2 builds and six music-pack tests passed, including the supplied PAL
archive audit: 55 accepted and the two known missing-sequence archives rejected.
The helper still has no production caller. Track metadata validation and safe
publication remain required before exposing set loading; public count stays 132.
No default engine path or PS2 file changed.


### Native BGM set loading (2026-10-20)

Added audio.bgm_load with bounded owned file reads, detached archive/config
preflight, decoded banks/sequences, registered-description checks and per-bank
track capacity checks before native stop. A successful explicit request installs
music A and optional ambient E, applies staged reverb and leaves the set stopped.
Playback is a subsequent audio.bgm_play request. No-mod loading is unchanged.
Effect-bank letters and ambient-only sets reject; missing ambient data is kept.
The API docs describe these boundaries and the pending decoded-memory budget.

Clang 23.1.2 builds and focused load/failure tests passed. Tests cover bounded
owned reads, missing descriptions, invalid archives, pending loads and unchanged
playing-state snapshots on rejection. All four PAL audio shared-action replays
passed, including changing sets, starting the new track and back-floor travel;
38 Python tests passed. The author default suite now has 37 scenarios. Public
operation count is 133. Owned music replacement, broader content APIs and
cross-platform validation remain unfinished.

Full regression: 878 passed, 97 skipped, one disabled. PAL evidence:
bgm-load-602e64c80cee4a7fb34de4b60ffa52a2/report.json under the local framework
expansion evidence folder. PS2 diff against the baseline is empty.


### Owned music replacement API (2026-10-20)

Added audio.music_set/clear and audio.state.music. The native sequence start
selects a registered package track or returns the exact previous soundtrack
fallback when no mapping exists. Runtime ownership isolates registrations;
conflicts name the existing owner. Clear/unload restores the prior fallback,
while a newer native start supersedes the binding. Scene cleanup releases active
bindings and preserves mappings. The official Kazarov backend is unchanged.

Focused tests cover actual mixed PCM replacement/restoration, competing owners,
native supersession, owner retirement, stopped-state cleanup, strict arguments,
sole-write transactions and deferred shared actions/callback rollback. Physical
listening, PAL replacement-specific replay and non-Windows validation remain
pending. Public operation count is 135; this does not close the other capability
gaps in the ledger.

Validation: Clang 23.1.2 builds; 882 native tests passed, 97 skipped,
one disabled; all 38 Python checks passed. PS2 diff remains empty.


### Owned music PAL replay and author tooling (2026-10-20)

The audio smoke suite now contains five scenarios (38 default scenarios). The
new replay declares a FLAC package asset, arms through the shared menu, registers
the currently loaded sequence, restarts its native track and confirms port zero
selected the owned replacement. It clears the mapping, verifies playing state
and completes native back-floor travel without mod diagnostics. This tests actual
native-to-runtime selection, rather than just an isolated registry lookup.

The read-only audio.state.music bound_ports field makes selection inspectable;
it is not an audibility/playing claim and does not promise a mapping update has
already replaced a previously bound asset. PCM output/restoration remains covered
by the separate mixer tests. Physical-device listening remains unverified.

Validation: Clang 23.1.2 builds, all five PAL audio scenarios, 12 focused
runtime/audio tests and all 38 Python checks passed. PAL evidence is
music-replace-2f9f8ffa1afa404e96d5a76aafae83b0/report.json in the local framework
expansion evidence folder. PS2 diff remains empty. Public operations stay 135.


### Actor ownership prerequisite (2026-10-20)

Audited the native monster setup path and pinned reference despawn path. Native
setup runs an initialization script; removing an actor must also retire attack
and projectile references and avoid kill rewards/mimic chest triggers. Calling
setup or clearing monster.state alone is insufficient for a safe public API.

Added internal generation-checked actor ownership metadata. Claims bind to an
identity registry, cannot be stolen by another owner, cannot transfer across
slot reuse/floor reset, and retain cleanup records while owner retirement work
is enumerated. Invalid/stale claims reject and storage is bounded by supported
slot capacity. The record stores a definition name without allocating custom
item/weapon/cutscene IDs or prescribing those contributor registries.

This is metadata only with no production caller or native state writes. Public
spawn/despawn still require resource preflight, native cleanup and gameplay
replays. Public operation count remains 135; the entity lifecycle gap is open.

Validation: Clang 23.1.2 game/test builds and all 20 ownership/entity-handle
tests passed. PS2 diff against the baseline remains empty.


### Loaded actor spawn resource preflight (2026-10-20)

Added internal read-only planning for a loaded retail monster model. It validates
finite bounded position values, the nine-model table bounds, script/model and
attachment-frame presence, and the exact first free slot native setup will use.
That slot must have at least the 448 quadwords reserved by BtSetEventScript;
the preflight must not skip an unready first slot in favor of a later arena.
Mimics reject because their map chest-trigger lifecycle needs separate handling.

The planner does not execute scripts, activate handles, grow the active limit or
mutate the RNG/native actor state. It does not yet validate floor placement,
custom bytecode contents or the game transition context. These are prerequisites,
not a claim that public spawn/despawn is implemented. No production caller yet;
public operation count remains 135.

Validation: Clang 23.1.2 game/test builds and six spawn/ownership tests passed.
Tests exercise missing resources, model bounds, first-slot arena capacity,
attachment requirements, full floors, ownership reuse and RNG preservation.
PS2 remains untouched; contributor-owned content registries are unaffected.


### Actor combat-reference cleanup prerequisite (2026-10-20)

Added a commit-only helper to retire hit/projectile references for a validated
monster slot. Collision hits match the native slot*5+200 owner identity or the
source monster index; projectiles match their secondary source-slot ID. Actual
array extents are used, with compile-time paired-array checks. Unrelated and
inactive records remain untouched, invalid slots fail closed, and repeated
cleanup is idempotent. The helper does not retire a native actor or grant kills.

Death-event queues can now cancel one unit/generation without dropping another
actor's events or changing their order. These helpers have no production caller
until owned spawn/despawn integrates resource, placement, lifecycle and sound
cleanup. Public operation count remains 135; no claim of completed despawn.

Validation: Clang 23.1.2 game/test builds and ten focused actor/event tests
passed, including expanded-slot and array-tail cleanup, unrelated references,
idempotence and unit/generation isolation. PS2 diff remains empty.
