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

Character IDs are 0â€“5 and must be joined; omitted means the controlled character.
Weapon slots are 0â€“10. Amount and HP arguments are integers 0â€“32767. Water values
may be fractional, finite and 0â€“32767. Lua arguments use a root object with string keys. Values can be finite numbers,
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
included. ZIPs must be unpacked for installation. The validator checks native
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
