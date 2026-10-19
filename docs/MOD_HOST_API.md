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
| `weapons.durability_set` | `slot`, `durability`, optional `character` | `game.write` | Set WHP gauge within native maximum; not repair-powder item use, broken-default-weapon conversion, or weapon-break logic |
| `story.flag_get` / `story.flag_set` | `id`, plus `value` for set | `game.read` / `game.write` | Global bit 0–8191, value 0 or 1 |
| `story.int_get` / `story.int_set` | `id`, plus `value` for set | `game.read` / `game.write` | Global integer 0–31, signed 32-bit value |
| `map.flag_get` / `map.flag_set` | `map`, `id`, plus `value` for set | `game.read` / `game.write` | Map 0–79, bit 0–511, value 0 or 1 |
| `map.init_flag_get` / `map.init_flag_set` | `map`, `id`, plus `value` for set | `game.read` / `game.write` | Map initialization bit, same bounds as map flags |
| `fishing.get` | none | `game.read` | Points and native capacity (9999) |
| `fishing.add` / `fishing.spend` | `amount` | `economy.write` | Add within native capacity; spending above balance rejects the entire batch |
| `georama.get` | `town` | `game.read` | Town 0–5: 24 saved plot records, placements until native sentinel, request completion entries |
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
queued menu actions, Lua tick, then native plugin tick. Subscribed ABI 2 native
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

## Dependency query exports

Require `services.query.v1` for shared read/calculation services. A provider declares
up to 32 uniquely named functions during initialization:

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
`chronicle.get`. Mutating cross-mod commands, arbitrary custom events, and native
plugin exports are still separate remaining work. This API does not implement
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
observed gameplay tick. Requests run once on the next gameplay tick in package
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
There are no arbitrary Lua UI controls or native-plugin action bindings yet.
