# Host operations and author tools

The framework preserves native capacities, generator paths, RNG, combat and
rendering when no gameplay mod is active. It installs no gameplay packages.
Host reads happen only when requested; host writes happen only after a successful
callback. Merely enabling a script that never calls a write API does not request
any native state changes. Native plugins are trusted code and can bypass these
host guarantees once explicitly enabled and approved.

## Lua calls

Declare `host.calls.v1` in `requires_capabilities` and the necessary permissions.
List mandatory operation names in `requires_operations` to fail at discovery
with an actionable manager message if implementation or permission is missing.
`chronicle.api_list()` returns implemented operations and metadata.
`chronicle.api_available(name)` checks implementation, not the current gameplay
phase, permission grant, or presence of a particular character.
`chronicle.call(name, arguments)` returns a detached query result, or `true` when
a write has been queued. Failure returns `nil, reason`. All calls below require
active town/dungeon gameplay. Unknown operations and fields fail explicitly.

| Operation | Arguments | Permission | Result / effect |
|---|---|---|---|
| `player.get` | optional `character` | `game.read` | Character, HP/capacity, defense, water/capacity, ailments, equipped slot |
| `party.active` | none | `game.read` | Same snapshot for controlled character |
| `party.list` | none | `game.read` | Joined character snapshots |
| `player.thirst_get` | optional `character` | `game.read` | Water/capacity and native drain/step enable flags |
| `inventory.get` | none | `game.read` | Capacity, bag slots and quick-use slots |
| `weapons.list` | none | `game.read` | Owned native weapons, character/slot, item, level, stats, WHP and ABS |
| `weapons.get` | `slot`, optional `character` | `game.read` | One owned weapon; fails for empty or unavailable slot |
| `economy.get` | none | `game.read` | Money and storage capacity (65535) |
| `world.get` | none | `game.read` | Native mode, map, dungeon, floor and restriction identifier |
| `player.heal` | `amount`, optional `character` | `game.write` | Add HP, clamped to current maximum |
| `player.damage` | `amount`, optional `character` | `game.write` | Reduce HP; direct gauge change, not a combat hit or damage formula |
| `player.set` | `hp`, optional `character` | `game.write` | Set current HP within maximum; does not set maximum HP |
| `player.thirst_set` | exactly one of `water` or `restore`; optional `character` | `game.write` | Set/add water within native capacity; stop pending water interpolation |
| `economy.add` | `amount` | `economy.write` | Add currency, clamped to wallet storage capacity |
| `economy.spend` | `amount` | `economy.write` | Spend currency; reject the whole callback batch if funds are insufficient |

| `player.max_hp_set` | `max_hp`, optional `character` | `game.write` | Set capacity (1–32767); clamp current HP and cancel HP interpolation |
| `player.max_water_set` | `max_water`, optional `character` | `game.write` | Set capacity (1–32767, fractional allowed); clamp water and cancel interpolation |
| `player.defense_set` | `defense`, optional `character` | `game.write` | Set base defense (integer 0–32767) |
| `player.status_get` | optional `character` | `game.read` | Native ailment mask and shared duration counter; does not introduce independent effect timers |
| `player.status_add` | `status`, optional active `character` | `game.write` | Apply freeze/stamina/poison/curse/goo using retail interactions and durations |
| `player.status_remove` | `status`, optional active `character` | `game.write` | Remove one named ailment; preserve other bits and any remaining timed effect |
| `weapons.durability_set` | `slot`, `durability`, optional `character` | `game.write` | Set WHP gauge within native maximum; not repair-powder item use, broken-default-weapon conversion, or weapon-break logic |
| `story.flag_get` / `story.flag_set` | `id`, plus `value` for set | `game.read` / `game.write` | Global bit 0–8191, value 0 or 1 |
| `story.int_get` / `story.int_set` | `id`, plus `value` for set | `game.read` / `game.write` | Global integer 0–31, signed 32-bit value |
| `map.flag_get` / `map.flag_set` | `map`, `id`, plus `value` for set | `game.read` / `game.write` | Map 0–79, bit 0–511, value 0 or 1 |
| `map.init_flag_get` / `map.init_flag_set` | `map`, `id`, plus `value` for set | `game.read` / `game.write` | Map initialization bit, same bounds as map flags |
| `fishing.get` | none | `game.read` | Points and native capacity (9999) |
| `fishing.add` / `fishing.spend` | `amount` | `economy.write` | Add within native capacity; spending above balance rejects the entire batch |
| `georama.get` | `town` | `game.read` | Town 0–5: 24 saved plot records, placements until native sentinel, request completion entries |
| `georama.elements_get` | `town`, `plot` | `game.read` | Loaded editor town only; plot 0–23, six optional-element slots with native ID, prerequisite slot, live and saved enabled state |
| `georama.element_set` | `town`, `plot`, `element`, `enabled` | `game.write` | Loaded editor town only; element slot 0–5, boolean enabled; updates live and saved state atomically |

| `dungeon.progress` | none | `game.read` | Seven native dungeon progress slots and their highest reached floors |

| `inventory.count` | `item` | `game.read` | Owned retail copies across bag and quick slots; world map uses its native ownership flag |
| `inventory.give` | `item`, `quantity`, `seed` | `game.write` | Grant existing retail bag items; reject capacity overflow |
| `inventory.remove` | `item`, `quantity` | `game.write` | Remove bag copies first, then quick-slot copies; insufficient ownership rejects the batch |
| `weapons.give` | `item`, `seed` | `game.write` | Grant an existing retail weapon to its joined native owner; do not equip automatically |
| `weapons.abs_get` | `slot`, optional `character` | `game.read` | Current ABS, native next-level threshold, level and synthesis points |
| `weapons.abs_add` | `slot`, `amount`, optional `character`, `synthesis` | `game.write` | Add ABS up to native threshold and synthesis points; no implicit level-up |
| `weapons.attachments_get` | `slot`, optional `character` | `game.read` | Fitted sockets, attachment stats and SynthSphere metadata |
| `attachments.list` | none | `game.read` | All 43 native attachment bag records, including the three overflow slots; capacity remains 40 |
| `attachments.give` | `item`, `quantity`, optional `level` | `game.write` | Grant existing retail attachments to free regular slots; stat boosters accept level 1-3 |
| `attachments.remove` | `bag_slot` | `game.write` | Discard one owned bag attachment, including an existing overflow record |
| `weapons.build_up_list` | `slot`, optional `character` | `game.read` | Native evolution targets, requirements, and current eligibility without consuming RNG |
| `weapons.synthesize` | `slot`, optional `character` | `game.write` | Status-break an unequipped level 5+ retail weapon into a SynthSphere, including fitted bonuses |
| `items.native_definition` | `item` | `game.read` | Read existing retail item/weapon metadata; does not register custom content |

Character IDs are 0–5 and must be joined; omitted means the controlled character.
Status add/remove target only the controlled character and require its native
status object to be active; an explicit benched character is rejected.
Weapon slots are 0–10. Amount and HP arguments are integers 0–32767. Water values
may be fractional, finite and 0–32767. Lua arguments use a root object with string keys. Values can be finite numbers,
booleans, UTF-8 strings, nested objects, or consecutive 1-based arrays. Empty Lua
tables represent objects. Limits: depth 8, 128 nodes (object keys count too),
128-byte keys without NUL, and 16 KiB encoded JSON. Cycles, mixed/sparse arrays,
functions and userdata are rejected without invoking metamethods. Each operation
still validates its own documented argument types and fields. Flag queries return `{value = number}`. Empty read arguments may be omitted. Native numeric IDs and slots do
not create persistent handles for mod-defined content.

Writes are queued in call order, at most 64 per callback. Queries observe native
state before that callback's writes, not staged state. A Lua error discards the
queue and rolls back the callback's owned scalar/quest state. A native validation
failure also rejects the entire batch and stops that module with a visible error.
Legacy `chronicle.add_money` participates in the same transaction and its net
delta is staged before the named commands. Prefer one currency API style within
a callback. Separate callbacks/mods commit in deterministic dependency order;
there is no transaction spanning multiple callbacks. Native plugin ticks and Lua
callbacks are separate transactions.

### Retail player statuses

`player.status_add/remove` accept exactly `freeze`, `stamina`, `poison`, `curse`
or `goo`. They do not accept numeric masks, custom names, knockout or duration
overrides. Use HP operations for knockout/revival. The add operation follows
`BtSetStatusErr`:

| Requested status | Retail behavior |
|---|---|
| freeze | Applies, removes stamina/poison/goo, sets the shared counter to 300 |
| stamina | Applies unless frozen; sets the shared counter to 1800 |
| poison | Applies unless frozen or under stamina; removes goo |
| curse | Applies without replacing other statuses |
| goo | Applies unless frozen, under stamina or poisoned |

If the requested status is absent after native interaction rules, the commit
fails and the entire callback rolls back. Reapplying freeze/stamina refreshes
its retail counter. Other statuses retain the existing counter on addition.
Removal clears only the named bit and clears the counter if neither freeze nor
stamina remains. Removing an absent effect is allowed. The engine advances the
shared counter for freeze/stamina and clears the native mask on expiration;
these APIs do not add independent timers or modify that retail behavior.

Native application sounds are deferred until the complete write batch commits;
rejected callbacks play none. Stamina/removal have no application sound. Statuses
are native saved gameplay mutations, not owner-scoped overlays restored when the
mod is disabled. No hit animation, damage, knockback or item consumption is added.
Queries in the same callback still see pre-commit state; inspect the next callback
to read the result. For example, in a registered menu action:

```lua
assert(chronicle.call('player.status_remove', {status='freeze'}))
assert(chronicle.call('player.status_add', {status='stamina'}))
```

Story and map writes update saved flags; they do not replay a cutscene, rebuild
live town objects, trigger transitions, or grant objects implicitly. Use actual
native flag IDs for the targeted game revision. Multiple mods editing the same
native field must declare a shared `exclusive` claim when they are incompatible;
the manager cannot infer semantic conflicts from arbitrary Lua code. These saved
changes persist in normal saves after a mod is disabled. Georama results describe
the saved reconstruction records, which can lag unsaved live town edits.

All save-field writes are staged on a copy only when a callback queues a write,
and committed in call order after the whole batch validates. No-mod execution
never takes this path. Character maximums and defense are persistent native stats,
not temporary owner-scoped tuning with automatic restoration.

HP changes update current/target HP, stop pending HP interpolation and update
the knockout bit. They do not run damage animations, invulnerability, armor,
weapon effects or other combat-hit logic. Damage formula hooks remain unsupported.

```lua
return {on_tick = function()
    if chronicle.get("rewarded") then return end
    local player = assert(chronicle.call("player.get"))
    if player.hp > 0 and player.hp < player.max_hp then
        assert(chronicle.call("economy.spend", {amount = 5}))
        assert(chronicle.call("player.heal", {amount = 10}))
        assert(chronicle.set("rewarded", true))
    end
end}
```

The example requires `game.read`, `game.write` and `economy.write`. It is illustrative
author code; no such healing behavior is bundled into active gameplay.

## Resources, diagnostics and legacy reads

`chronicle.resource(id)` reads an owned manifest resource as a byte string, with
`resources.read` permission. Files are checked and cached at package startup,
never opened from callbacks. Limit: 16 files, 64 KiB each. `chronicle.log(level,
message)` accepts `info`, `warning`, `error`, at most 512 bytes; the session retains
128 messages. Logs appear in manager details and are not stored in saves.

The existing `chronicle.player()`, `party()` and `inventory()` reads still work,
with `game.read`. Their legacy field names differ (`id`, `slots[].id`, `party.members`);
use the catalogued calls for new code. All returned tables are detached copies.

## Offline author tools

Run from the checkout with Python 3.9 or newer:

```text
python tools/chronicle_mod.py new author.my-mod --output ../my-mod
python tools/chronicle_mod.py check ../my-mod
python tools/chronicle_mod.py inspect ../my-mod
python tools/chronicle_mod.py check-set ../mods --enable author.my-mod
python tools/chronicle_mod.py pack ../my-mod --output ../my-mod.zip
python tools/chronicle_mod.py install ../my-mod.zip --mods ../save/mods
python tools/chronicle_mod.py capabilities
python tools/chronicle_mod.py apis
```

Repeat `--enable ID` to preview a selected set. With no `--enable`, nothing is
selected. `check-set` reports duplicates, missing dependencies, cycles, transitive
blocks, declared conflicts, exclusive claims and file collisions. Native trust
is always deferred to the game manager. None of these commands enables packages,
changes profiles, loads native libraries or executes Lua.

`check`, `inspect` and `check-set` accept `--data PATH` for target existence and
exact-size checks against an extracted logical asset tree. Without it, only
package structure is checked. This is not a live extraction-index validator.
`pack` creates a new ZIP without overwriting an existing file; only declared
assets/resources/scripts/library and standard README/license/changelog files are
included. ZIPs must be unpacked for installation, manually or with `install`. The validator checks native
file shape/size, not machine architecture or executable safety.

## Same-seed regression tool

Export `ModVanillaAudit.SeedMatrix` from pinned upstream and this build by setting
`AUDIT_OUTPUT` to a separate JSONL path for each run. Set `AUDIT_MODS` only for the
installed-but-disabled package run. Then compare all bytes, without dropping RNG,
room-link, chest, or other native fields:

```text
python tools/chronicle_mod.py compare-seeds upstream.jsonl empty.jsonl disabled.jsonl
```

A mismatch exits nonzero, reports hashes/counts and the first differing record
(if only final line terminators differ, that record is null). This compares existing
exports; it does not build upstream or run the game. Matching results prove only
the fixture coverage described in [MOD_PARITY.md](MOD_PARITY.md).

## Retail inventory adapter

These operations are implemented separately in `port/src/mods/retail_inventory.cpp`.
They do not define a custom item or weapon format, registry, ID allocation scheme,
or content-loading hook. Contributor boundaries are in
[MOD_CONTRIBUTOR_BOUNDARIES.md](MOD_CONTRIBUTOR_BOUNDARIES.md).

Bag item IDs are 132–256; weapon IDs are 257–376; definition inspection also accepts
attachment IDs 81–130. IDs still need an actual matching native definition. A numeric
ID is revision-specific retail data, not a persistent custom-content handle.
Quantity is 1–100. `seed` is required for grants and is an integer 0–4294967295.

Grants use native base definitions, with explicit local random state for feather
volume, amulet charges, and non-default weapon durability traits. The local stream
is unsigned 32-bit `seed = seed * 1664525 + 1013904223` per random draw; it never
calls retail `rand`. Repeating the same request reproduces its properties. Authors
can persist and advance their chosen grant seeds in owned mod state. This policy
deliberately does not reproduce the global RNG sequence of retail pickup helpers.
The generated item or weapon remains an ordinary native inventory record.

Grants reject over-capacity bags, weapon overflow slots, and weapons belonging to
unjoined characters. No array sizes change and no overflow menu is forced open.
World-map grants/removals operate its native flag and require quantity one.
Removal orders bag slots before quick slots, updates `item_count`, and never
silently discards more items than requested. Attachment inventory uses the separate
`attachments.*` operations below. Item use and weapon equip remain separate work.

ABS `amount` and optional `synthesis` are integers 0–32767. Synthesis addition
rejects signed-32-bit overflow; ABS clamps to the native threshold. This does not
consume attachments, run level-up random rolls, build up, or status-break a weapon.
Existing equipped weapons remain equipped. All writes participate in the same
callback transaction as health, currency and story flags.

## Offline installation and updates

Close Chronicle before changing installed package files. Install archives produced
by `pack` into a chosen mods directory:

```text
python tools/chronicle_mod.py install ../my-mod.zip --mods ../save/mods
python tools/chronicle_mod.py install ../my-mod-v2.zip --mods ../save/mods --replace
```

The installer validates in a hidden staging directory before publishing a package.
It rejects traversal, symlinks/special files, case-colliding paths, Windows device
names, undeclared files, encrypted members and unsupported compression. Limits:
8,192 entries, 64 MiB per member and 512 MiB total uncompressed data. The archive
must contain exactly one folder matching its package ID. Optional `--data` adds
native target size checks, as with `check`; no code is executed.

An existing destination requires `--replace`. The previous valid package moves to
`<mods>/.backups/<id>-<unique token>` before replacement. A failed publication
restores that backup when the destination remains free. Hidden staging/backups
are ignored by discovery. Backups are retained; no old package is silently deleted.
To revert, close the game, move the replacement aside, and move the recorded backup
back to its original package folder. Keep package updates and saved-game backups
separate: this operation does not migrate or restore gameplay saves.

The installer never writes `mods-profile.json`, enables a mod, approves native
code, or resolves conflicts by deleting other packages. New IDs without a prior
local selection stay disabled; replacements retain existing selection. Updated
native bytes still require renewed trust. Restart, then inspect the shared menu
for dependency versions, conflicts, permissions and native approval. Updating an
enabled Lua package changes its code on restart, so use explicit updates only.

## Owned lifecycle listeners

Require `events.lifecycle.v1` when these events are mandatory. During the script's
initial execution, call `chronicle.subscribe(event, function(world) ... end)`.
At most 32 subscriptions belong to one package; they disappear when its Lua runtime
is replaced or stopped. Late registration and unknown event names return `nil, reason`.

| Event | Trigger |
|---|---|
| `tick` | Every active gameplay simulation tick, after entry notifications |
| `map_enter` | First observed gameplay map or change from the previous observed gameplay map |
| `floor_enter` | First observed dungeon floor or changed dungeon/floor; town snapshots use -1/-1 |
| `game_enter` | First gameplay tick or return after a non-gameplay mode |
| `game_leave` | First non-gameplay tick after gameplay; game reads/writes are unavailable |
| `save_loaded` | First gameplay tick after successful mod-state load or reset; restored owned state is available |

Callbacks receive `mode`, `map`, `money`, `playing`, `dungeon`, and `floor`.
Menus that keep the same gameplay mode do not manufacture entry/leave events.
Floor entry reports observed status, not a promise that generation or scene assets
are ready. Separate native-ready hooks remain future work.
A non-gameplay interval does not itself repeat map/floor entry; returning to town
and then entering a dungeon does. Failed save loads do not emit `save_loaded`.

Existing returned callbacks such as `on_tick` remain supported. The corresponding
`on_<event>` callback runs first, then subscribers in registration order, with fresh
world tables. They share one instruction budget and transaction per event: failure
rolls back that event's owned-state changes and queued native writes, stops the
module and skips its later listeners. Previously committed events remain committed.
The order within a package is save-loaded, game enter/leave, map entry, floor entry,
enemy-kill events, queued menu actions, Lua tick, then native plugin tick. Subscribed ABI 2 native
callbacks follow the corresponding Lua lifecycle callback/listeners, in separate
transactions; see [the native SDK](MOD_NATIVE_SDK.md). Packages retain dependency load order. World values
are the snapshot supplied for that tick, not an implicit refresh after each write.

```lua
assert(chronicle.subscribe('floor_enter', function(world)
    chronicle.log('info', 'Entered floor '..world.floor)
end))
return {}
```

These are runtime lifecycle notifications, not enemy death, dialogue, cutscene,
render, or native script-interception hooks. No listener runs with no active module.

## Enemy-kill events

Require `events.enemy_killed.v1`. Register during startup with
`chronicle.subscribe('enemy_killed', function(world) ... end)` or return an
`on_enemy_killed` callback. ABI 2 plugins request `CHRONICLE_EVENT_ENEMY_KILLED`.
Capture remains disabled without an active Lua/native subscriber.

The callback receives the normal world snapshot plus `world.event` containing
`entity` (opaque, already-dead handle), `attacker` (-1 when unknown/scripted),
`reward_exp`, `reward_money`, `damage` (native last-hit value, possibly -1), and
`source` (`native` or `scripted`). Reward fields are enemy parameters, not a
receipt of rewards paid. Native poison deaths use `native`; `combat.damage`
deaths use `scripted`. Despawning/clearing a floor is not a kill event.

Native damage processing only captures bounded values. On a later runtime tick,
events dispatch in capture order after lifecycle events and before menu actions
and tick callbacks. Each Lua event shares its listeners' transaction; its native
callback has a separate transaction. A failed listener rolls back its changes
and stops that package, without rolling back the death itself. Payload tables are
detached. No callbacks run inside native damage processing.

Events are not saved or replayed after load/reset. Floor cleanup, unit changes,
gameplay exit and runtime replacement discard stale pending events. Capture holds
64 records; overflow drops later records and produces a runtime diagnostic. Mods
must not treat this bounded notification stream as a durable kill journal.

## Dependency query exports

Require `services.query.v1` for shared read/calculation services. A provider declares
up to 32 exports in total (queries and commands) during initialization:

```lua
assert(chronicle.export_query('price', function(request)
    return {amount = request.level * 10, currency = 'gilda'}
end))
return {}
```

A consumer lists the provider ID in `requires` (optionally constraining its version),
then calls during gameplay:

```lua
local quote, reason = chronicle.invoke_query('author.provider', 'price', {level = 3})
assert(quote, reason)
assert(quote.amount == 30)
```

Requests and results are detached structured objects with the common argument
limits. Export names use the owned-state key syntax: 1–64 lowercase letters,
digits, underscore or dot. Returned arrays/scalars/functions are not a valid root
result. A provider can call its own declared providers. The outer callback shares
at most 16 invocations, including nested calls, and a maximum depth of eight;
reentry into an executing provider is rejected. Each provider call has the normal
Lua instruction/memory limits. Missing, failed or undeclared providers, invalid
results and provider errors return `nil, reason` to the consumer. A handled query
error does not terminate the provider or poison its next invocation.

Query exports cannot call managed state setters, quest updates, currency writes,
or native write operations. Host reads use the **provider's** permissions; an
export is an intentional read/calculation service, not a transfer of native write
authority. Lua-local variables and diagnostic logs are not transactional save data;
authors should keep query calculations stateless and read persistent values through
`chronicle.get`. Mutating dependency commands and package-owned custom events
are documented below. Native plugin exports remain separate work. This API does not implement
custom item/weapon registration or the contributor's cutscene framework.

## Retail attachment bag and status-break

`attachments.list` returns `{capacity=40, slots=[...]}`. All 43 records include
`bag_slot` (zero-based), `overflow`, `item`, `sphere_weapon`, `sphere_flags`,
`sphere_level`, and numeric arrays `stats` (attack/endurance/speed/magic),
`elements` (fire/ice/thunder/wind/holy), and `monster_stats` (native order).
Empty records have `item < 81`; their other values are reported as zero.
Byte-sized stat and socket arrays are numeric JSON/Lua arrays, never strings.
Lua arrays are one-based even though native slot selectors are zero-based.

`attachments.give` takes an existing attachment ID in 81-130 and quantity 1-40.
There must be enough free regular slots and no occupied overflow slot. Copies use
the native definition. The four stat boosters (91-94) add `level` to the matching
stat; it defaults to 1 and accepts 1-3, matching native acquisition levels. Other
attachment IDs reject a supplied level. SynthSpheres (90) cannot be fabricated by
this call: use `weapons.synthesize` on an owned weapon. No custom registration or
arbitrary attachment stat input is introduced.
ID 131 is rejected: its native metadata points past the 50-entry definition table.

`attachments.remove {bag_slot=0..42}` discards the complete record, including any
SynthSphere data, and fails for an empty or invalid record. It does not remove
fitted attachments or rearrange other bag slots. Slot numbers identify the current
inventory position, not a persistent identity. These operations leave native
capacity and the native overflow-notification lifecycle unchanged.

`weapons.synthesize {slot=0..10, character=optional}` requires a joined owner,
matching retail weapon, level 5-99, an unequipped slot, and free regular attachment
space without unresolved overflow. It performs the native status-break value
calculation without menu animations or resource loading: base and fitted bonuses
are rounded separately at 60 percent, doubled sockets and native caps apply, and
opposed weapon flags cancel through the native helper. Fitted attachments are
consumed into the sphere. Non-default weapons are removed; default weapons reset
to their retail starting record. It neither auto-equips a replacement nor changes
the equipped slot. The operation, bag writes, and other queued commands share the
same save transaction; a later failure discards all of them. No global RNG is used.

`weapons.build_up_list` inspects an owned weapon without altering it. Each target
has `item`, `meets_requirements`, `attachments_empty` and `can_build`. Ordinary
targets also expose `requirements` with attack/magic and element/monster arrays;
negative native requirements are upper bounds on the absolute value. Mardan
targets instead report `fish_required` and `fish_caught` from this save's native
counter (5 and 15 catches respectively). Attached values participate in the native
requirement comparison, but building also requires removing fitted attachments.
This query does not perform evolution or register a custom evolution path.

```lua
-- Requires game.read and game.write. Queries see the pre-callback inventory;
-- inspect the new bag records on a later tick or action after the commit.
assert(chronicle.call("attachments.give", {item=81, quantity=2}))
assert(chronicle.call("attachments.give", {item=91, quantity=1, level=3}))
local targets = assert(chronicle.call("weapons.build_up_list", {slot=0}))
```

Fitted attachment insertion/removal, active equipment refresh, actual build-up,
and automatic level-up remain separate APIs to implement. The contributor still
owns custom item/weapon registration and content formats.

## Mod manager actions

Require `menu.actions.v1`. During script initialization register up to 32 named
callbacks; return the usual callback table afterwards:

```lua
assert(chronicle.action("show_progress", "Show progress",
    "Record the current objective progress in the package log.", function(world)
        assert(chronicle.log("info", "Progress: " .. tostring(chronicle.quest_get("visit"))))
    end))
return {}
```

The example requires a declared `visit` quest. Registering an action adds a button
under **Mod Manager → R1 Actions** for the running package. It does not execute the
callback. IDs use 1–64 lowercase ASCII letters, digits, underscores or dots and
must be unique within the package. Titles/descriptions are 1–256 valid UTF-8 bytes
without control characters. UI text is literal, including braces. Actions are
listed by ID. Registration after initialization is rejected with `nil, reason`.
No new manifest section, content format or custom-item ID space is reserved.

Cross/Run queues the selected action; Circle/Back returns to package management.
The runtime accepts at most 16 pending actions globally and rejects duplicate
owner/ID requests until dispatched. It requires a live script and a previously
observed gameplay tick for Lua actions. Native actions require a live ABI 2 plugin
instead; see [the optional native menu extension](MOD_NATIVE_SDK.md#native-menu-actions).
Requests run once on the next gameplay tick in package
dependency order, after lifecycle entry callbacks and before the package's tick
callback; requests within one package retain queue order. The callback receives
the normal world table and its own instruction/call budget. Queries, declared
permissions, dependency query exports, owned state and queued game writes follow
the same rules as lifecycle callbacks. The menu never invokes Lua directly.

An action's successful callback commits its transaction; a Lua error or rejected
commit restores its owned state and discards its pending writes, then stops the
package and records diagnostics. Earlier callbacks' committed changes remain.
The action pane displays queued/completed/failed/canceled status; “Completed”
means the callback and its transaction succeeded, not that the framework checked
an author's gameplay objective. Native plugin callbacks are separate transactions.

Pending requests are canceled if gameplay stops or mode/map/dungeon/floor changes
before dispatch, or discarded when loading/resetting a save or restarting the
runtime. Requests, registrations and UI results are not saved. Failed/unloaded
packages cannot execute actions;
disabled packages register none at startup. Changing enable selection still
requires restart, so actions shown before restart belong to the current run.
Native actions share this list, package namespace, 32-action limit and scheduling
rules; their callback receives the C host/state and a JSON world snapshot instead
of a Lua table. Arbitrary custom UI controls remain unimplemented.

### Scripted enemy damage and combat state

`combat.get()` requires `game.read` and an active dungeon. It returns `dungeon`,
`floor`, the native `alive_count`, and the save's `recorded_kills` for that floor.
Alive count is native bookkeeping, including enemies not returned by the revealed
enemy list; it is not necessarily the length of `entities.list().entities`.

`combat.damage({entity = handle, amount = 1..1000000})` requires `game.write` and
the current dungeon's native player-status context. The target must be active,
revealed, alive and identified by a current handle. Invincible targets accept the
command without losing HP. Otherwise HP falls by the requested amount, clamped
at zero. The callback stages damage, hit metadata and the pending native reaction
together; failed batches publish none of them. Commands after a lethal hit cannot
heal or modify that dying target in the same batch.

Native stepping dispatches the queued hit/death script after commit, so paused
simulation can delay death accounting. Death decrements native alive count once,
increments recorded kills (saturating at 32767), clears stop and marks the native
death event flag. The native death script handles subsequent drops/rewards; this
API does not fabricate collision hits, elemental effects, knockback or weapon
credit. Scripted hits use attacker -1. Damage formulas remain pending; kill notifications use the enemy-kill event API. Read `combat.get` on later callbacks to observe death processing.

Pending reactions survive a mod stopping because the HP change is already
committed. Native slot construction and floor cleanup cancel obsolete reactions.
The bounded queue supports two complete retail units; calls reject capacity
exhaustion before publishing any state. No queue is created or processed beyond
an empty check when there are no requested reactions.

### Enemy healing and native status timers

`combat.heal({entity = handle, amount = 1..1000000})` queues healing clamped to the
enemy's native maximum HP. `combat.status_add({entity = handle, status = "slow" |
"stop", duration_frames = 1..32767})` extends that native timer to the greater of
its current value and the requested duration. Both require `game.write` and a
live, revealed, actively participating enemy in the current dungeon. Dormant,
dead, stale and out-of-dungeon targets reject the callback. Read the resulting
fields with `entities.get` on a subsequent callback after commit.

These operations participate in the same atomic callback as save and Georama
writes: a failure in any domain publishes none of them. Healing does not simulate
a hit, revive enemies, award kill credit or change rewards. Status calls set the
requested timer directly, without a random weapon-proc roll, and do not clear
other ailments. Native stepping controls animation, timer progression and expiry;
inactive simulation may delay those effects. This is not persistent per-mod stat
ownership or a damage-formula API.

### Owned live enemy stats

`entities.set_stats` and `entities.reset_stats` require `game.write`, an active
dungeon and a current handle for an active, revealed, living enemy. Both Lua and
native plugins use these calls. Declare them in `requires_operations` when needed.

```lua
assert(chronicle.call('entities.set_stats', {
  entity = enemy.entity, max_hp = 300, defense = 40, reward_exp = 80
}))
-- In a later callback, release only this package's changes to this enemy:
assert(chronicle.call('entities.reset_stats', {entity = enemy.entity}))
```

Provide at least one stat; omitted fields remain unchanged. Unknown fields,
nonintegers and out-of-range values reject the entire callback transaction.

| Field | Allowed values |
|---|---|
| `hp`, `max_hp` | 1–1,000,000; HP must not exceed the resulting maximum |
| `defense`, `reward_exp` | 0–32,767 |
| `reward_money` | 0–2,147,483,647 |
| `drop_item` | -1 clears the forced drop; existing retail IDs 81–376 except invalid attachment 131 |

These are native enemy parameters, not guaranteed rewards: normal native drop,
reward and death-script conditions still apply. Clearing a forced drop restores
normal selection; it does not suppress all drops. There is no custom item registry.
Changing maximum HP without an explicit HP scales current HP proportionally,
rounding down with a minimum of one for a living enemy. Commands later in the same
callback see the staged maximum for validation/healing. Public queries continue
to read committed state until that callback finishes.

The runtime supplies the package owner; scripts/plugins cannot impersonate another
owner through arguments. Different packages may own different fields. A competing
claim rejects the whole callback with the field and existing owner's ID in the
manager diagnostic. Maximum-HP writes also claim HP to prevent split ownership.
No partially applied fields or claims survive failed save, scene or entity writes.

Reset releases the caller's fields only. Owned changes are also released when the
module fails, the runtime/save state is replaced, dungeon gameplay ends, or native
code clears/reuses the slot. Module cleanup happens at the game-bridge boundary;
changing manager enable selections still requires restart. Claims are session-local
and are not saved. Cleanup also covers units that are no longer being inspected.

Unchanged owned values restore their pre-claim baseline. Later native changes are
preserved; a new explicit write rebases its baseline if native code superseded the
last requested value. If maximum HP is restored after damage/healing, health keeps
its current fraction of maximum (rounded down); zero HP is never resurrected.
This is conditional restoration, not a continuous stat lock: native phase scripts
can replace values. Identical native writes cannot be distinguished from unchanged
values. Live stats do not modify spawn templates, collision sizes or damage formulas.
The port keeps at most eight units of ownership records, each with the unchanged
16 retail enemy slots. No records are created without explicit stat commands.

### Owned enemy visual scale

`entities.scale_set({entity = handle, scale = multiplier})` requires `game.write`
and a current handle for an active, revealed, living enemy in a dungeon. The
uniform multiplier must be finite and between 0.5 and 3.0. It multiplies the
captured native X/Y/Z scale; repeated requests use that baseline rather than
compounding the previous multiplier. The native `CCharacter::SetScale` path
updates both the main model and shadow. No draw-loop override is installed.

`entities.scale_reset({entity = handle})` releases only the calling package's
scale ownership. `scale = 1` keeps ownership while requesting the captured native
size; use reset to release it. A competing package's scale write rejects the
entire callback with the existing owner's ID in the manager diagnostic. A reset
by another owner has no effect. Both operations reject unknown fields and obey
the same ordered, atomic save/entity transaction boundary as stat writes.

Scale ownership is independent of stat ownership and is not saved. Failure or
unload, runtime/save replacement, leaving dungeon gameplay, floor clearing and
slot reuse release it. Each axis restores only if it still equals the mod's last
applied value; later native changes are preserved. A subsequent explicit scale
request rebases axes that native code changed, keeping the baseline of untouched
axes. Native phase scripts can supersede scale; this is not a continuous lock.
Invalid native scales and floating-point overflow reject the transaction before
changing model, shadow or claims. Ownership is bounded to eight units of 16 retail
slots and stays empty without explicit requests.

This changes visual transforms, not monster HP, native body/collision radius,
movement parameters or spawn templates. Attached visuals/effect positions that
follow model frames can follow the scaled model. It does not provide a physical
resizing or hitbox API. The existing enemy snapshots expose `visual_scale` as an
array of the three currently committed native scale components, including native
changes made after the request.

### Live enemy inspection

`entities.list` takes no arguments and requires `game.read` plus an active dungeon.
It returns `{entities = [...]}` in native slot order, excluding dead, hidden and
unused enemies. `entities.get({entity = handle})` returns the same detached record
or `{entity = handle, status = "expired"}`. A record contains `entity`, `status`,
`hp`, `max_hp`, `defense`, `reward_exp`, `reward_money`, `drop_item`, `active`,
`slow_frames`, `stop_frames` and `visual_scale`. `active` distinguishes participating from dormant
revealed enemies. Reward fields are native parameters, not guaranteed payouts.

Handles are opaque decimal strings; preserve them exactly. They are session-local
and must not be stored in saves or reused across game processes. Slot construction,
floor cleanup, changing the observed dungeon unit, leaving dungeon gameplay and
runtime/save-state replacement expire old handles. These APIs neither spawn nor
damage enemies. Tracking begins only on an explicit query and uses the existing
16 retail slots. Calls from unavailable gameplay callbacks remain subject to the
runtime's normal gameplay gate.

Georama element edits use existing retail definitions only. Slots whose native ID
is outside 0–35 are unavailable. Enable prerequisites first; disable dependent
elements first. Ordered commands in one callback see earlier staged changes, and
any failure rolls back the whole callback. These calls do not grant Atla, place
buildings, award completion rewards, or register custom content. They reject an
unloaded town or unavailable plot. No scene scan occurs unless a mod calls them.
The query returns `{town, plot, elements}`; each element has `element`, `native_id`,
`optional`, `required_element` (-1 means none), `enabled`, and `saved_enabled`.
Returned integer IDs retain their integer type in Lua and may be passed directly
to host calls. Native/save state validation does not establish visual refresh for
every retail element type.

### Enemy spatial queries and movement intent

These operations use existing session-local enemy handles through Lua and the
native host-call boundary. Declare them in `requires_operations` when mandatory.
They require an active dungeon. No physics loop or AI/collision override is installed.

- `physics.query({center = {x,y,z}, radius = r})` (`game.read`) finds active,
  revealed, living enemy **origins** within an inclusive sphere. Center coordinates
  must be finite within +/-1,000,000; radius must be finite from 0 to 10,000.
  Returns `{entities = {{entity = handle, position = {x,y,z}, distance = n}, ...},
  semantics = ...}`, in native slot order, at most 16 entries. Hidden, inactive,
  dead and nonfinite origins are omitted. This is not a mesh, hitbox, raycast or
  line-of-sight test; zero radius matches an exact origin.
- `physics.velocity_get({entity = handle})` (`game.read`) returns `entity`,
  `velocity = {x,y,z}`, `units = "game_units_per_frame"`, `source` and `semantics`.
  Source is `native_movement`, `knockback` or `stopped`. The snapshot combines
  native direction/speed (or current knockback) with the native falling term;
  stop yields zero. This is current movement intent, not measured displacement
  or a prediction of the next simulation step. Subsequent AI, knockback decay,
  collisions and native restrictions can change motion. Invalid native movement,
  stale handles and inactive enemies fail the query.
- `physics.velocity_set({entity = handle, velocity = {x,y,z}})` (`game.write`)
  queues a one-shot command for an active, revealed, living enemy. Each component
  must be finite within +/-100,000. It normalizes native direction/speed,
  compensates for the current falling term and clears current knockback speed.
  It does not teleport, disable collisions, remove stop or continuously override
  AI. A stopped enemy rejects the command, including stop queued earlier in the
  same callback. All unknown fields are rejected.

Writes commit only after the complete save/entity batch succeeds. Queries during
a callback see committed state, not pending commands. Sequential commands follow
callback order; separate callbacks follow deterministic runtime package/listener
order. The later successful movement command replaces the earlier one. Like
damage, these are immediate effects rather than persistent ownership claims:
unload does not rewind movement or restore an obsolete AI vector. No controller,
deferred movement job or restoration record survives the command. Native
slot/floor cleanup resets movement and expires handles. Use manifest `exclusive`
claims when two packages must not control the same behavior.

```lua
local hits = assert(chronicle.call("physics.query", {
    center = {0, 0, 0}, radius = 1000
})).entities
if hits[1] then
    local current = assert(chronicle.call("physics.velocity_get", {
        entity = hits[1].entity
    }))
    if current.source ~= "stopped" then
        assert(chronicle.call("physics.velocity_set", {
            entity = hits[1].entity, velocity = {1, 0, 0}
        }))
    end
end
```

## Package-owned custom events

Require `events.custom.v1` when mandatory. `events.emit` uses `game.write` and
active gameplay, from Lua `chronicle.call` or either native host-call ABI:

```lua
assert(chronicle.call('events.emit', {
    name = 'challenge.complete', payload = {challenge = 'bridge', score = 42}
}))
```

Arguments contain exactly `name` and `payload`. The name is 1-64 lowercase
letters, digits, underscores or dots. Payload must be an object using the common
structured-argument bounds (depth eight, 128 nodes, encoded request at most
16 KiB). Owner identity is always assigned by the runtime; callers cannot supply
or spoof it. Custom names cannot impersonate native/lifecycle events.

Register listeners only during initialization. A consumer must list the provider
in `requires`; self-subscriptions are also allowed. Custom and lifecycle listeners
share the existing 32-listener limit per package.

```lua
assert(chronicle.subscribe_custom('author.challenges', 'challenge.complete', function(world)
    assert(world.event.owner == 'author.challenges')
    local score = world.event.payload.score
    assert(chronicle.set('last_score', score))
end))
return {}
```

Callbacks receive the normal world snapshot plus
`event = {owner = 'author.challenges', name = 'challenge.complete', payload = {...}}`.
Each listener receives a detached table. Lua listeners for one source/name share
one callback transaction; a native event callback is a separate transaction.
ABI 2 listeners opt in with `CHRONICLE_EVENT_CUSTOM` and receive custom events
from their own package and declared dependencies, filtering `event.name` as needed.
ABI 1 can emit using its existing host call, but has no event callback interface.

Emission is staged with the sender's callback and published only after all its
managed/native writes commit. The return value means queued, not delivered.
Query exports cannot emit. There are at most 16 emissions per callback and 64
queued globally between ticks. Exceeding the local bound returns an error; if
publication would overflow the global queue, the entire sender callback fails
before any game writes commit, restores its managed state, and stops that package
with a manager diagnostic. Game writes and emissions share success/failure, but
remain separate bounded queues. Ordinary event-only callbacks need no game-write
provider beyond the permission/active-gameplay checks.

Delivery occurs on the next runtime tick, after lifecycle notifications and before
native death events, menu actions and normal ticks for each receiving package.
Packages follow dependency order; each package receives events in publication
order. Emissions from listeners wait for the following tick, never recurse within
the current tick. Listener failure rolls back its changes and stops that package;
it does not rewind the sender's successful callback or other recipients.

These notifications are transient, not a durable message log or synchronous
command RPC. Events are discarded on gameplay exit, changed mode/map/dungeon/floor,
successful load/reset/restart, or an unavailable/failed sender. They are not saved
or replayed. No listeners, native hooks or engine behavior are added when packages
do not use the capability. Native-ready events and native plugin exports remain separate APIs still to implement.

## Transactional dependency commands

Require `services.command.v1` when mandatory. A Lua provider registers a named
function during initialization using `chronicle.export_command(name, function)`.
Queries and commands share a maximum of 32 exports per package. Names follow the
1-64 lowercase-letter/digit/underscore/dot rule. The same name can identify a
query and a command; each category rejects its own duplicates.

```lua
assert(chronicle.export_command('reward', function(request, caller)
    -- caller is assigned by the runtime, never taken from request data.
    if caller ~= 'author.challenge' or request.amount ~= 5 then
        return {accepted = false, reason = 'unsupported request'}
    end
    assert(chronicle.set('rewards', (chronicle.get('rewards') or 0) + 1))
    assert(chronicle.call('economy.add', {amount = request.amount}))
    return {accepted = true}
end))
return {}
```

The example provider needs `economy.write`. Consumers declare the provider in
`requires`, need `game.write`, and call during a gameplay callback:

```lua
local result, reason = chronicle.call('services.command', {
    provider = 'author.rewards', name = 'reward', arguments = {amount = 5}
})
assert(result, reason)
assert(result.accepted, result.reason)
```

The request has exactly `provider`, `name` and an object `arguments`. Requests
and object results use the common bounded structured transport; arrays/scalars
are not valid roots. Input/output tables are detached. Native ABI 1/2 callers use
the same operation and JSON shape through `host->call`; the response is the
provider's JSON object, not a generic queued boolean. Providers are currently Lua
functions, including scripts in packages that also contain native plugins. This
does not yet supply native-function export descriptors.

Exports are intentional permission delegation: game reads/writes inside them use
the **provider's** permissions and trusted `GameCommand.owner`. A consumer's
`game.write` alone cannot directly use `economy.write`, but a provider with that
permission can explicitly offer a reward service. Providers must validate inputs
and authorize callers where appropriate. The second Lua argument is the immediate
caller's package ID, including at each nested dependency call. Engine ownership
belongs to the provider; disabling a consumer does not implicitly release the
provider's effects. Providers should expose release commands for temporary leases.
No contributor content IDs, schemas or registries are prescribed by this API.

The result is provisional until the outer callback commits. Root and provider
managed values/quest progress form one transaction. Their game commands retain
call order in one shared batch; legacy `add_money` deltas still aggregate before
that batch. Custom emissions retain each provider's identity and publish only
with the outer commit. A failed consumer, provider exception/invalid result,
instruction limit, rejected/throwing game commit, or event publication overflow
restores all participating managed state and publishes no queued effects. Earlier
independently completed callbacks remain committed.

An invoked provider error makes the outer transaction uncommittable, even if its
caller ignores `nil, reason`. Missing exports/providers and exceeded invocation
limits also fail that transaction. The root package stops with a manager diagnostic;
a provider is not stopped merely because its exported command failed. An expected
business rejection should return an object such as `{accepted=false, reason=...}`
before writing anything. Permission/schema/undeclared-dependency errors reject
that individual host request before invoking any provider and can be handled.
Lua locals, closures, diagnostic logs and native plugin private memory are not
managed save state and cannot be rolled back; use `chronicle.set`/quest APIs for
transactional persistent state and keep temporary calculations local to a call.

Queries and commands share 16 service invocations per outer callback and maximum
depth eight. Provider reentry is rejected; calls flow only to declared dependencies.
Nested commands also share the root's 64 game-write limit, 16 custom-emission limit
and bounded legacy currency delta. Each Lua execution retains its instruction and
memory limits. Query exports cannot invoke mutating commands. Game queries still
observe committed native state, while managed state reads can see staged provider
changes within the same outer transaction. There is no background dispatcher or
engine hook; registration and transaction state are rebuilt on load/reset/restart.
