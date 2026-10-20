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
| `weapons.equip_state` | none | `game.read` | Town loadout readiness/reason and saved equipped slots for joined characters |
| `weapons.equip` | `slot`, optional `character` | `game.write` | Select an owned retail weapon slot while walking outdoors in town |
| `player.combat_state` | none | `game.read` | Native dungeon-player readiness, invincibility/recoil timers and action gauge |
| `player.invincible` | `frames` | `game.write` | Extend the native one-shot invincibility timer to at least 1–600 frames |
| `player.tuning_state` | none | `game.read` | Effective dungeon tuning and owners per control |
| `player.tuning_set` | selected tuning fields | `game.write` | Replace this package's owned tuning request atomically |
| `player.tuning_clear` | none | `game.write` | Release this package's tuning request |
| `shops.list` | none | `game.read` | All 18 native shop lists, base prices, resource owners and write readiness |
| `shops.get` | `shop` | `game.read` | One native shop (0–17), capacity 20, stocked goods and write readiness |
| `shops.set_items` | `shop`, `items` | `game.write` | Own and replace a native list with up to 20 existing retail item IDs |
| `shops.register_item` | `shop`, `item` | `game.write` | Append one retail good; duplicates allowed, native capacity enforced |
| `shops.remove_item` | `shop`, `item` | `game.write` | Remove the first matching occurrence; fail if absent |
| `shops.reset_items` | `shop` | `game.write` | Release this package's list and conditionally restore its original contents |
| `shops.set_price` | `item`, `buy`, `sell` | `game.write` | Own shared base prices for one retail item; both integers 0–32767 required |
| `shops.reset_price` | `item` | `game.write` | Release this package's shared price pair and conditionally restore it |
| `player.get` | optional `character` | `game.read` | Character, HP/capacity, defense, water/capacity, ailments, equipped slot |
| `party.active` | none | `game.read` | Same snapshot for controlled character |
| `party.list` | none | `game.read` | Joined character snapshots |
| `party.switch_state` | none | `game.read` | Dungeon switch readiness, per-member reasons and last deferred request |
| `party.set_active` | `character` | `game.write` | Request native switch to a joined retail character (0–5) after the frame; exclusive engine write |
| `player.thirst_get` | optional `character` | `game.read` | Water/capacity and native drain/step enable flags |
| `inventory.get` | none | `game.read` | Capacity, bag slots and quick-use slots |
| `weapons.list` | none | `game.read` | Owned native weapons, character/slot, item, level, stats, WHP and ABS |
| `weapons.get` | `slot`, optional `character` | `game.read` | One owned weapon; fails for empty or unavailable slot |
| `economy.get` | none | `game.read` | Money and storage capacity (65535) |
| `world.get` | none | `game.read` | Native mode, map, dungeon, floor and restriction identifier |
| `world.locations` | none | `game.read` | List the five supported retail settlement map IDs and names |
| `world.transition_state` | none | `game.read` | Native travel readiness, rejection reason and last request state |
| `dungeon.locations` | none | `game.read` | Seven retail dungeons, numeric IDs and native floor counts |
| `dungeon.enter` | `dungeon` (0-6), optional `floor` | `game.write` | Native entry; optional zero-based floor, sole engine write |
| `world.transition` | `map` (0–4) | `game.write` | Queue native settlement travel as the callback's sole engine write |
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
| `weapons.refresh_state` | none | `game.read` | Native equipment readiness and detached cached combat values |
| `weapons.attachment_set` | `slot`, `socket`, `bag_slot`, optional `character` | `game.write` | Fit/swap an owned bag attachment; equipped changes require readiness |
| `weapons.attachment_remove` | `slot`, `socket`, optional `character` | `game.write` | Move a fitted attachment to a free regular bag slot |
| `weapons.upgrade` | `slot`, `seed`, optional `character` | `game.write` | Level up using ABS or Powder; equipped changes require readiness |
| `weapons.build_up` | `slot`, `target`, optional `character` | `game.write` | Evolve a retail weapon after rechecking requirements; active model changes remain gated |
| `weapons.synthesize` | `slot`, optional `character` | `game.write` | Status-break an unequipped level 5+ retail weapon into a SynthSphere, including fitted bonuses |
| `items.native_definition` | `item` | `game.read` | Read existing retail item/weapon metadata; does not register custom content |
| `items.use` | `item`, optional `character` | `game.write` | Consume one owned retail healing, water or ailment-cure item atomically |
| `items.quick_slots` | none | `game.read` | Saved quick-slot contents plus native selection readiness/reasons |
| `items.quick_slot_select` | `slot` (1–3) | `game.write` | Select an already-loaded dungeon quick slot after transaction validation |
| `items.quick_slot_use` | `slot` (1–3) | `game.write` | Select and request a bounded native Square-button action; acceptance is not consumption |

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

### Deferred native character switching

`party.set_active` is a resource-loading request, not an atomic save edit. It must
be the callback's only engine write, including writes from dependency commands and
legacy currency calls. A successful call queues the command; successful callback
validation publishes the request. Native loading runs after `MGEndFrame`, outside
the frame that used the old character resources. Do not treat the call's return
value as proof that loading completed.

`party.switch_state` returns `ready`, `reason`, `character`, `members` (each with
`character`, `allowed`, `reason`) and `request`. The request begins as
`{state="idle"}`; thereafter it reports `state` (`pending`, `complete`, `failed`
or `cancelled`), `owner`, `from`, `target` and `reason`. This is the most recent
request across all mods. Only one request can be pending; a second caller receives
the current owner's conflict reason. Use the existing shared-manager action API
to offer a switch button, and let an `on_tick` callback submit it once ready after
the menu closes.

Switches require stable dungeon play, synchronized native character/weapon
resources, a joined living target with a valid equipped weapon, and no action,
jump, event, presentation, first-person view, restrictive character floor, active
knockout or blocked Osmond ground. The host rechecks these conditions at execution
and cancels requests whose owner stopped or whose runtime epoch changed. It also
checks required character, weapon, effect, voice and menu files before and after
queuing. `ready` describes gameplay readiness; missing or invalid assets can still
reject a request. Preflight checks file availability, not arbitrary pack validity.

The native loader updates the model, weapon/effect resources, attachment statistics,
voice and loaded-character cache. Selecting the already active member is a no-op.
Completed switches persist like a player's native switch and are not reversed on
mod disable. Failed callbacks publish no request; a later load failure is reported
through `request` and does not roll back already committed mod scalar state. Native
resource loading is not a rollback-capable transaction. Town-character selection
and party recruitment remain separate APIs, not aliases for this request.

With no request pending, the end-of-frame hook returns immediately without reading
game state, checking assets or loading resources.

### Town character selection

`party.town_state` (`game.read`, empty arguments) and `party.town_set_active`
(`game.write`, exactly `{character=0..5}`) select a joined retail body for outdoor
town walking. IDs are Toan, Xiao, Goro, Ruby, Ungaga and Osmond. This is temporary
presentation ownership; it does not change the dungeon active character or weapons.
Use a shared mod-manager action to arm an `on_tick` request after the menu closes.

The write must be the callback's only engine write, including legacy currency.
Success queues a request; native loading occurs after the frame ends. Read back
`state`, `reason`, `owner`, `character` (last successful selection), `pending_owner`,
`target` and `restore_pending`. Additional fields are `ready`, `unavailable`,
`joined` and `storage_bytes`. Readiness is not ownership: another mod's lease or a
missing asset can reject a request. A partial failure is not proof that the last
successful character is still displayed. Loading is not a rollback transaction.

Readiness requires outdoor walking in a retail town, outside menus, events,
interiors and first-person view. The host rechecks readiness, membership and asset
availability before loading. Availability checks do not validate arbitrary pack
contents. Replacement packs must satisfy the native loader's format requirements.

Only one mod owns the selection. Selecting Toan releases it. Owner shutdown,
runtime restart or loss of joined membership requests Toan restoration; unsafe
scenes or missing assets defer cleanup with a reason. Native scene retirement
releases ownership and storage. Mod storage is allocated only for a replacement
and released after restoration or retirement. Renderer records are retired before
storage reuse/unmapping. No-mod post-frame hooks return without loading resources.
Goro's unavailable motion 32 uses idle for the corresponding town motion.

Retail tests exercise all six bodies through a shared-manager action, runtime
shutdown/restoration and native map-transition retirement. Full movement,
conversation, interior and cross-platform coverage remains outstanding.

### Town weapon loadouts

`weapons.equip` takes `slot` (integer 0–10) and optional `character` (integer 0–5,
defaulting to the saved active dungeon character). The character must have joined;
the exact slot must contain a compatible native weapon. Duplicate copies are
unambiguous because the slot is mandatory. Empty/incompatible slots, invalid
existing equipped indices and malformed native weapon records fail validation.
Selecting the already equipped slot leaves the save unchanged.

`weapons.equip_state` returns `ready`, `reason` and `slots`, an array of saved
`{character, slot, item}` entries. Those entries describe the save even when the
scene cannot accept an equip request. Readiness requires a loaded retail town,
outdoor walking, no menu/event/interior, and valid party state. It uses the current
native mode and map, not potentially stale dungeon-floor fields in the save.

The operation changes the saved loadout transactionally. Earlier writes in the
same callback are visible, so granting and equipping a known free slot can be one
batch. A later failure rolls back the slot change along with currency and other
save writes. It does not transfer weapons between characters, grant unowned
weapons, reset weapon stats or request a resource reload while walking in town.
The normal dungeon-entry loader uses the selected slot for the model/effects.
The selection persists as an ordinary save change after the mod is disabled.

Dungeon equipping is explicitly rejected, as in the pinned reference. Live
model/effect switching remains separate work. Offer the command using an existing
shared-manager action and wait for `weapons.equip_state.ready` after the menu
closes. No loadout gameplay mod is installed or enabled by the framework.

### One-shot player protection

`player.invincible` accepts exactly `{frames=1..600}` with an integer duration.
The current dungeon player and save must be synchronized and native player
resources available. `player.combat_state` reports `ready` and `reason`; when ready
it also returns `character`, `invincible_frames`, `recoil_frames` and `action_gauge`.
Outside a valid dungeon context it omits those values rather than returning stale
combat data. Requests do not take a character ID or caller-supplied owner.

The write shares callback transaction validation with other engine writes. Any
failed save/entity/shop/tuning write cancels the timer request too. At commit,
the timer becomes the maximum of its native value and all requested durations;
shorter requests never remove an existing longer protection interval. Only this
timer changes; HP, recoil, gauge and equipped resources are untouched by the call.

This is a one-shot native effect, not an owned tuning lease. After commit the
native movement loop counts down the timer, including its normal pause rules.
Disabling the caller does not cancel an already granted interval; scene changes
follow native reset behavior. Frames are simulation updates, not wall-clock
seconds. It follows native hit checks, not immunity from every HP mutation:
explicit `player.damage`, scripted damage or thirst can have separate semantics.
Nothing runs or changes without a requested write.

For a shared-manager action, queue `chronicle.call("player.invincible",
{frames=120})` once gameplay resumes and inspect `player.combat_state` on a later
tick. Do not continuously refresh the timer unless persistent protection is the
intended gameplay mod behavior. No protection mod is included or enabled.

### Owned player tuning

`player.tuning_set` accepts an object containing any of these fields. Omitted
fields release that package's previous claims; `{}` is equivalent to clear.

| Field | Accepted values | Native effect |
|---|---|---|
| `run_speed` | finite multiplier 0.25–4 | Dungeon walk/dash speed |
| `attack_anim_speed` | finite multiplier 0.25–4 | Safe recovery portion of retail Toan sword swings; hit/sound windows and the final completion window retain native stepping |
| `recoil_scale` | finite multiplier 0.25–4 | Newly applied hit recoil, rounded and at least one frame |
| `gauge_refill` | finite multiplier 0.25–4 | Native action-gauge gain after character/status rules |
| `thirst_drain` | finite multiplier 0.25–4 | Native water drain after floor/weapon modifiers; disabled drain and drinking behavior remain native |
| `durability_wear` | finite multiplier 0.25–4 | Base per-hit wear; the native hit-kind surcharge remains additive |
| `invincible_bonus_frames` | integer 0–120 | Invincibility through scaled recoil plus this bonus, never shortening existing invincibility |
| `map_reveal`, `monster_reveal` | boolean | Temporary minimap Map/Crystal behavior; does not grant inventory items |

Effects apply in dungeon mode. Requests can be configured during town or dungeon
gameplay and survive ordinary scene changes until cleared or their owner stops.
Each field has one owner. Different mods can own different fields; a competing
claim fails with the field and existing package ID. Explicit `1`, `0` and `false`
values still claim their controls. There is no implicit multiplier stacking.

`player.tuning_state` returns `active`, `epoch`, `effective` and `owners`, with
each control represented in the latter two objects. `active` means claims exist,
even in town where effects are dormant. Writes share callback transaction
validation with save/entity/shop writes; query readback observes committed state.
A failed transaction publishes neither new tuning nor partial claims.

Ownership is reconciled before gameplay updates and after script callbacks.
Runtime restart, disabled/failed owners and explicit clear stop future tuning.
Already consumed water/WHP or hit timers are not rewound. These requests are
ephemeral, not save data; reapply deliberately after a runtime restart if desired.
With no claims, hooks return without scaling native values or scanning owners.
Neutral values also preserve the original float operations.

The swing multiplier applies to the current native recovery speed and is capped
before the completion window. Unlike the reference's absolute recovery speed,
it is a multiplier. Retail Toan's first swing has no eligible interval under these
guards and stays unchanged; the other four have verified recovery intervals.
Other characters, custom motion layouts, cutscenes and contributor frameworks
are not assigned new motion contracts by this API.

Use `chronicle.action` in the existing shared manager to arm a tuning request,
then submit it from `on_tick` after gameplay resumes. No tuning gameplay package
is bundled or enabled. Windows PAL tests cover native thirst/wear calculations,
gauge and swing recovery, host calls, clear and runtime-stop cleanup. Full rendered
minimap comparisons, live movement/hit scenarios and other platforms remain
unverified.

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

When testing a deliberate storage expansion, add `--allow-map-size-change` to
compare every parsed field except the diagnostic `map_bytes`. Raw `matches`,
hashes and first differing records remain in the report; `map_size_only`, both
`map_sizes` and `first_non_size_difference` make the exception explicit. This
mode requires positive integer sizes and still rejects changed RNG, grid
dimensions, rooms, links, boxes, extra cells or missing records. A successful
size-only comparison establishes the exported fixture values, not identical
memory footprint or all runtime behavior. The default command remains strict.

## Reproducible retail smoke scenarios

Use Python 3.11+ and an existing Clang CMake build containing `darkcloud_tests`.
The runner reads the build's C++ compiler metadata and rejects non-Clang builds;
it does not build the executable. Follow the platform build instructions first.
Supply your extracted PAL data and a **new** output directory:

```text
python tools/chronicle_mod_smoke.py --build build-mods --data ../pal-data --output ../smoke-results
```

The default runs twenty-six built-in scenarios: seven retail back-floor transitions,
seven public 256-monster layouts, seven owned chest-loot scenarios, and five
population lifecycle/rejection cases.
Use repeatable `--suite retail`, `--suite population`, `--suite chests`, or `--suite lifecycle` to
select a subset. `--timeout 120` controls the per-process deadline in seconds.

Each scenario runs sequentially in a fresh process with its own save profile,
audit package, XML result and `process.log`. The packages exercise the shared
menu-action API. The runner does not install them in your normal mod directory.
Output inside the supplied data tree or any `ps2` directory is rejected. Existing
output directories are rejected rather than reused. The same graphics, driver,
and runtime-library prerequisites as the test executable still apply; on Windows,
put llvm-mingw and SDL DLL directories on PATH as for the normal test command.
Avoid running another graphics test simultaneously.

`report.json` records the executable SHA-256, compiler metadata, selected suites,
per-scenario status, duration and logs. It is updated after each scenario so an
interrupted run remains visibly incomplete. A successful exit requires every
selected test to execute and pass: skips, missing or mismatched XML results,
zero-test runs, process errors and timeouts all fail the command. Timeout kills
and waits for that scenario's process before continuing. Inherited `GTEST_*`
and `DC_*` overrides are cleared for the child processes so sharding, filters
and old audit paths cannot silently change the requested run.

Compiler metadata records the configured toolchain, not an attestation of binary
provenance. These tests validate the built-in framework scenarios, not arbitrary
third-party packages, every layout/seed, sustained combat or other platforms.
They complement the separate same-seed comparisons above. Retail assets and
output artifacts stay local; do not include them in distributed mod packages.

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
are documented below. Native exports are supported through services.native.v1. This API does not implement
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

Loading a new active weapon model or Ruby effect form remains to implement.
Same-resource equipped changes use the native refresh path described below. The contributor still
owns custom item/weapon registration and content formats.

### Fitting and removing retail attachments

`weapons.attachment_set {slot, socket, bag_slot, character=optional}` moves an
owned attachment from bag slot 0-42 into an existing weapon socket (0-5, further
bounded by its retail definition). If occupied, it swaps the old attachment back
into the same bag slot. Complete records are preserved, including SynthSphere
provenance, level, flags and values; no new attachment is created. A SynthSphere
requires a native type-2 socket. Socket multipliers remain attached to the weapon,
not to the moved attachment.

`weapons.attachment_remove {slot, socket, character=optional}` returns a fitted
attachment to the first free regular bag slot (0-39). A full bag, unresolved
overflow, empty socket or invalid record rejects the transaction. Fitting may
consume an explicitly selected overflow record, enabling recovery; swapping it
leaves the displaced record in that same position. Removing never creates overflow.

Both operations require `game.write` and a joined owner. Equipped weapons require
a ready native equipment context as described below.
Declare the operation names in `requires_operations` when mandatory. They run
through the shared Lua/native host boundary, apply the retail active-element
correction, consume no global RNG and roll back with all other queued writes.
Active model or Ruby effect-form changes remain unavailable. Slot coordinates are
mutable positions; inspect current inventory before selecting them.

```lua
-- Inside a gameplay callback or shared-manager action:
assert(chronicle.call('weapons.attachment_set', {
    character=0, slot=1, socket=0, bag_slot=0
}))
-- An upgrade queued after this fit sees the newly fitted attachment.
-- Queries see the committed result on a later callback.
```

Use `attachments.list` and `weapons.attachments_get` for selection and verification.
The operations do not register custom attachments or weapons and add no default
packages, periodic scans or engine hooks.

### Retail weapon level-up

`weapons.upgrade {slot=0..10, seed=0..4294967295, character=optional}` performs one
retail level-up. Declare it in `requires_operations` and grant `game.write`.
The weapon must belong to a joined character and be below level 99. Equipped
weapons additionally require native readiness as described below.
If ABS meets the native threshold, no item is consumed. Otherwise exactly one
owned Powerup Powder is consumed, bag first then quick slots. Missing payment,
invalid attachments, synthesis counter overflow or a later failed command rolls
back the whole transaction, including payment and absorbed attachments.

The operation applies the menu's attachment calculation, doubled socket values,
native caps and option handling, adds one to attack/endurance/speed/magic, adds
1-3 to WHP capacity up to 99, resets ABS, increments level and records absorbed
SynthSpheres. Current WHP is preserved; this is not a repair. All six fitted
attachment records are cleared on successful completion. Only the first five
eligible, non-permanent sockets are rerolled, matching the retail menu's bounds.

Random draws use the same package-local 32-bit LCG as retail grant operations,
with the supplied seed; eligible socket rolls precede the capacity roll. Once a
double socket is selected, later eligible sockets consume no further rolls.
Results are deterministic for the same input weapon and seed, but do not promise
the same outcome as the retail global RNG. No global RNG state is read or changed.
The framework does not automatically level up weapons when ABS is granted.

```lua
-- Run from a gameplay callback or shared-manager action. Queued writes see
-- earlier staged writes; queries see committed state until the next callback.
assert(chronicle.call('weapons.abs_add', {slot=1, amount=32767}))
assert(chronicle.call('weapons.upgrade', {slot=1, seed=42}))
```

Equipped upgrades use native readiness and refresh as described below.
This API consumes existing retail definitions only; custom item/weapon frameworks
remain contributor-owned. No package or gameplay change is enabled by default.

### Retail weapon evolution

`weapons.build_up {slot=0..10, target=257..376, character=optional}` evolves an
owned weapon to one of its retail build-up targets. Unequipped weapons work
without a native context; inactive party members' equipped weapons require readiness.
Changing the active weapon model remains unavailable. Declare
`weapons.build_up` in `requires_operations` and grant `game.write` (plus
`game.read` for inspection). Both Lua and native callers use the normal host call.
The owner must be joined. The host rechecks target membership, native stat/fish
requirements and empty attachments at commit, against prior commands in the same
transaction. `build_up_list.can_build` describes retail requirements; an equipped
active slot still cannot evolve through this API until model loading exists.

Evolution applies the retail completion data changes: destination ID, level/ABS
reset, destination minimum endurance/speed/WHP capacity and native destination
option flags. Other fields retain retail behavior, including current WHP and
socket data. Mardan evolutions clear the same save's catch counter. All changes
roll back together if any command fails; no global RNG or menu animation runs.
Slot indices are mutable inventory positions, not stable weapon handles. Recheck
inventory before queuing an action and inspect committed results on a later tick.

```lua
assert(chronicle.action('evolve', 'Evolve weapon', 'Build up the weapon in slot 1.', function()
    local targets = assert(chronicle.call('weapons.build_up_list', {slot=1}))
    for _, target in ipairs(targets) do
        if target.can_build then
            assert(chronicle.call('weapons.build_up', {slot=1, target=target.item}))
            return
        end
    end
    error('No eligible retail target')
end))
```

This example requires `menu.actions.v1` and uses the shared manager. It selects the
first eligible target; an author offering multiple paths should let the player
choose. No custom weapon definition, path registration or contributor ID is added.

### Equipped weapon readiness and refresh

`weapons.refresh_state {}` (`game.read`) returns `{ready, reason}`. When ready it
also includes active `character`, `slot`, `item`, `element` and `cached` combat
values: level, attack, magic, endurance, speed, max_durability and flags. Cached
combat values include native attachment and WHP-rate calculations; they are not
necessarily the raw saved weapon values. This query is an explicit observation,
not a subscription or polling loop installed by the engine.

Readiness requires stable dungeon play or first-person view, no native event,
a loaded weapon frame, the active native save binding and a synchronized weapon
ID/element cache. Town scenes, loading and character/menu transitions return false.
This is readiness to try a transaction, not a guarantee that every mutation can
succeed: the host rechecks the final staged weapon before committing anything.

When ready, equipped level-up and fitted attachment mutations can commit without
changing the active weapon's item/model or Ruby's effect form. Equipped build-up
of an inactive party member is also supported. Active build-up or a Ruby element
change that needs another effect resource rejects the whole callback, preserving
save, bag, payment, fish counters and queued entity effects. Equipped status-break
remains unavailable because it requires selecting a replacement weapon.

After successful commit, the bridge refreshes the native combat cache, weapon
colour and the retail Lamb's Sword damaged-model visibility. It performs no file
loads, model replacement or scene allocation. It does not alter already-created
projectiles. Failed batches emit no refresh. The bridge examines equipment only
for explicit weapon writes (or this query); no installed/enabled gameplay package
means none of this runs.

A shared-manager action can request an upgrade and wait until play resumes:

```lua
local requested = false
assert(chronicle.action('upgrade', 'Upgrade equipped weapon',
    'Upgrades once after you resume play; requires ABS or Powerup Powder.',
    function() requested = true end))
return {
    on_game_leave = function() requested = false end,
    on_tick = function()
        if not requested then return end
        local state = assert(chronicle.call('weapons.refresh_state'))
        if not state.ready then return end
        assert(chronicle.call('weapons.upgrade', {slot=state.slot, seed=42}))
        requested = false
    end
}
```

Declare `menu.actions.v1`, both operation names and `game.read`/`game.write` in the
manifest. Handle missing payment/other errors according to your package's UI;
this small example uses assertions, so a failed batch stops its script.

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
do not use the capability. Native-ready events remain outstanding.

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
provider's JSON object, not a generic queued boolean. Providers may be Lua functions or native ABI 2 services.

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

## Native dependency services

`services.native.v1` provides optional ABI 2 query/command exports; see
[the SDK](MOD_NATIVE_SDK.md#native-service-exports). Lua/native callers use
`services.query` (`game.read`) with the same `{provider,name,arguments}` object as
`services.command`, receiving the exported object. The older Lua invoke_query
helper retains its existing permission semantics. Declared dependencies, active
gameplay and shared 16-call/8-depth budgets apply. Native queries deny managed
state writes, events and commands through the host; invalid results are recoverable.
Native command failures poison the outer transaction. These guarantees cover
host-managed operations, not trusted DLL private memory or arbitrary native code.

## Owned retail shops

The `shops.*` operations expose the existing 18 native lists with capacity 20.
`shops.list` returns `{shops:[...], writable}`; `shops.get` returns
`{shop,currency:"gilda",capacity:20,items:[...],owner,writable}`. Each stocked
entry contains `{slot,item,buy,sell,price_owner}`. Empty owner strings mean no
framework owner. Queries read committed tables; queued writes appear next tick.
Declare required operations and `host.calls.v1` in the package manifest.

Stock accepts existing retail attachments, dungeon items and weapons. ID 131 is
invalid, and generic SynthSphere stock is rejected because it lacks owned weapon
provenance. Duplicates are retained; removal deletes only the first occurrence.
An empty `items` array clears a list. These operations do not register item or
weapon definitions, allocate custom IDs, change native capacity or create shops.
Lists take effect when the native shop next builds its goods on opening/refill.

Prices are shared **base** buy/sell values for an item across all shops, including
selling elsewhere. They are not a per-shop discount or a final quote for every
owned weapon: native pricing can include fitted attachments and other calculations.
`shops.set_price` requires both `buy` and `sell`, each an integer from 0 to 32767.

One package exclusively owns each whole stock list and each item price pair.
A competing write or reset fails with the existing owner's package ID. Repeated
owner updates retain the first baseline unless native code has independently
changed the resource. Reset, disable, failure or runtime restart releases claims
and restores their baseline only if the current resource still equals the last
framework write. External native changes are preserved. Claims are session state,
not saved overrides; packages must explicitly request them again after loading.

Writes require `writable:true`: stable town walking or stable dungeon play, outside
events and transitions. Town menus, conversations and the editor reject writes;
cleanup is also deferred until leaving those town modes so an active purchase is
not repriced. A shared-manager action should set a package flag and perform the
write from `on_tick` after readiness becomes true. Do not blindly retry a queued
write after a failed callback: a commit failure stops that package.

Stock, prices, ownership and other supported save/entity writes commit together.
Any validation, ownership, capacity or later batch failure publishes none of them.
No new engine hook, native RNG draw or default gameplay package is introduced.
Without claims, lifecycle cleanup does not scan or rewrite native shop tables.

### Retail item use

`items.use {item, character?}` requires `game.write`. The item is an existing
retail numeric ID; character is a joined index 0–5 and defaults to the active
character. Queue it through `chronicle.call`, including from an action registered
with the shared menu framework. The effect and consumption commit with the
callback's other writes; a failed command publishes neither.

Supported definitions are the native HP-only food and water-only consumables,
single-ailment cures with native character-use flags, and Mighty Healing.
Bread, Premium Chicken, Cheese, the three waters, Antidote Drink, Soap and
Mighty Healing are supported. Holy Water has different native use flags and is
not included. Random foods, revival, stamina, throwable items, Mellow Banana,
permanent stat foods and weapon-targeted effects are not handled by this API.
Custom-item registration remains reserved for the contributor's framework.

HP healing uses the native SetNextLife interpolation, including its existing
pending-heal behavior. Water uses the native integer-truncated two-point
threshold and caps at maximum water. Cures preserve the native timer; Mighty
Healing clears ailments while preserving stamina. Ineffective use fails without
consumption. Cures reject knocked-out characters. Water follows native rules
and does not require a living target.

Consumption takes the first matching regular bag slot, otherwise the first
nonempty matching quick slot. Overflow bag slots are not usable sources. Native
item volume and cached item count are preserved, as in DeleteItemAfterUseItem.
This is a one-shot saved effect: disabling the mod does not refund the item or
undo healing. No polling, new menu, engine hook, native RNG draw or custom
registration is introduced.

### Native quick-slot selection

`items.quick_slots {}` returns `ready`, `reason` and `slots`. Each slot reports
one-based `slot`, retail `item`, `quantity`, `ready` and `reason`. With no save,
slots is empty. Saved contents remain readable outside the dungeon; `selected`
is present only when the native context is ready. Top-level readiness describes
the context; check the individual slot's readiness before selection.

`items.quick_slot_select {slot=1}` requires `game.write` and a runtime owner.
Slots use native numbering 1–3. Selection requires ordinary dungeon play,
action-ready player state, no event, synchronized live save/player resources,
and a nonempty saved slot whose item matches its loaded model/cache. It does not
load missing resources, equip items into slots or consume an item. Empty slots,
stale caches, menus, first-person camera and active actions reject with a reason.

Selection is staged with other writes. The selected slot is revalidated against
the final staged inventory before publishing the save and both native selector
fields. A later failure or removal of the selected item rolls back the entire
callback. Multiple valid selections in one callback leave the last selected.
The change is one-shot, not an ownership lease; ordinary player input may change
it, and disabling the mod does not restore a previous selection. Lua and native
plugins use the existing host call and shared-manager action interfaces.

`items.quick_slot_use {slot=1}` selects the slot and queues a native Square-button
request after the callback commits. It uses the same readiness checks as
selection and additionally requires a recognized native quick action. Only one
use request can be pending globally. A callback may contain one use request,
with no later selection/use command; later ordinary writes still participate in
the transaction and final slot validation. Failure publishes neither selection
nor input. The existing shared-manager action interface can initiate the call.

The request supplies at most four subsequent game frames of held input and one
press through the existing native dungeon action branch. It does not directly
apply item effects or consume inventory. Native drinking, eating, throwing and
feather handling retain their own eligibility, animations, sounds and effects.
An ineffective or blocked action may consume nothing. In particular, native
throwing can enter aiming and still require ordinary player input to finish.
This API does not automate targeting. Acceptance must not be treated as success.

For the native throw path, aiming still reads the player's physical Square hold.
Without that hold, a mod request proceeds into the ordinary throw animation;
with Square held, the player can aim and release to throw. The request does not
replace physical aiming input. Retail tests cover one Toan stone throw both
without physical input and with an overlapping hold/release, verifying a native
projectile, exactly one consumed stone, and no replay over the next 90 frames.
Other throwable types and playable characters still require runtime coverage.

`items.quick_slots` also reports `use_pending` and, while pending, `use_owner`
and `use_slot`. A cleared request is not a consumption receipt: drinking may
already be running under native control. Inspect inventory/effect state to
confirm the result. Disabling/stopping an owner, runtime reset, expired deadline,
scene/character/slot changes, unavailable resources or a busy player cancel
pending input. Arena resets cancel before resources are retired. Cancellation
does not rewind an action that native code has already started.

Selection alone does not synthesize input. Native quick-slot use differs from
inventory-menu `items.use`; the two operations must not be substituted for each
other. With no pending request, the added native input check returns immediately
without reading game resources, scanning owners or changing physical input.

### Native settlement travel

`world.locations {}` returns the five retail destinations: map 0 Norune, 1
Matataki, 2 Queens, 3 Muska Lacka and 4 Yellow Drops. This read does not load
assets. `world.transition {map=1}` requests travel from ordinary outdoor town
walking or ordinary dungeon play. It rejects menus, events, active dungeon
actions, first-person view, missing native resources and an existing native or
mod transition. A request also checks the destination's indexed event resource;
this is not exhaustive validation of every asset that the native loader uses.

Travel must be the callback's only engine write, including dependency commands
and currency changes. Owned script state may still record the request. The host
queues it only when the callback succeeds and dispatches after the display frame
ends. It rechecks owner/epoch, save, scene, character, native readiness and the
destination resource before dispatch. A stopped owner or changed context prevents
travel. A second pending request reports a conflict with its owner. Requesting
the current town is an unchanged no-op.

`world.transition_state {}` returns `ready`, `reason`, and `request`. Request
states are `idle`, `queued`, `unchanged`, `dispatched`, `cancelled`, or `failed`;
non-idle states include the owner and destination, with an error on failure.
Dispatched means the native transition was initiated, not that arrival is
complete. Observe `world.get` and native readiness before subsequent operations.
A shared-manager action should arm an on_tick request after menus close.

The town path schedules the native system event used by world-map travel. The
dungeon path sets the native exit flag so GameLoop performs its ordinary
exit cleanup, including gate-key handling, character/status recovery and audio
handling under the native flags. A pending quick-item request is cancelled
before travel. Once dispatched, stopping the requesting mod does not rewind
travel. The operation can bypass ordinary story destination availability when
explicitly requested; it does not manually alter story unlocks.

This API accepts existing settlements only. Dungeon entry, specific floors,
back floors and registered map destinations remain separate coverage work.
There is no per-frame readiness/resource scan when no request is queued, and no
travel is triggered by merely installing or enabling a package.


### Native dungeon entry

`dungeon.locations {}` returns seven records containing `dungeon`, `name`, and
`floor_count`. `dungeon.enter {dungeon=0, floor=1}` requests the second floor of
Divine Beast Cave. Floor indices are zero-based and must be smaller than the
reported count. Omitting `floor` retains the native entrance selection menu.
This does not register maps or increase native dungeon capacities.

Like settlement travel, entry must be the callback's sole engine write,
including currency. It uses `world.transition_state` readiness and request
reporting. The reported map is 200 plus the dungeon ID; floor -1 means native
selection. A dispatched request indicates travel started, not arrival.
Explicit floor selection uses the native direct-entry path and can bypass
normal progression selection; authors must enforce their own story conditions.

The floor request is bound to the requesting runtime owner, epoch and save.
It is prepared after the destination initializes and consumed once by the
native entrance menu. Owner shutdown, runtime replacement, a different save or
destination, or a second scene initialization discards it. Canceling the floor
request after travel has begun does not undo travel; native floor selection
remains available. With no floor request the native entrance argument stays -1.


### Native back-floor transport

`dungeon.back_floor_state {}` (`game.read`) returns `ready`, `reason`, `active`,
`dungeon`, `floor`, `pending`, and the last `request`. When native resources are
available, `floor_gate_key` reports its item number and carried count. This is
the floor exit key, not proof that a back-floor transport item is held.

`dungeon.back_floor_enter {}` (`game.write`) queues entry as the callback's sole
engine write, including currency. It requires ordinary dungeon play and loaded
procedural front/back maps with a recognized transport event. Already being on
the back floor is an unchanged no-op. Pending requests identify their owner and
expire after 1,800 game frames; owner/runtime/save/floor changes cancel them.
Native requests are never overwritten. Readiness does not guarantee completion.

Dispatch starts the native transport event, including its confirmation and
animation. Moon Sea uses event 201; the other retail dungeons use event 200.
The caller bypasses the transport-item requirement through a scoped native flag
query; inventory and saved event flags are not changed. No scoped access exists
without a mod-started transport. Once dispatched, native travel continues if
its mod stops. Context retirement or native return to play releases the access.
`dispatched` means transport started; cancellation at the native confirmation
can still leave the player on the front floor. Inspect `active` to confirm arrival.

This does not register destinations, change floor generation or increase engine
capacities. Boss/free-layout floors or floors without a recognized transport
reject. Retail validation covers the first floor of all seven dungeons with
Toan on PAL Windows, not all floors, characters, regional assets or platforms.


### Saved and active dungeon restriction rules

`dungeon.floor_rules_get {dungeon=0, floor=0}` (`game.read`) returns `dungeon`,
`floor`, the saved `rule`, and `active_rule`. Both indices are required and
zero-based. The native saved table covers dungeons 0-5 and at most 25 floors;
queries reject floors beyond that dungeon's retail count. Demon Shaft has no
entry in this table.

`dungeon.floor_rules_set {dungeon=0, floor=0, rule=11}` (`game.write`) changes
only the saved rule. Defined values are -1 (none), 0-5 (required character),
10 (no weapon changes), and 11 (accelerated thirst). IDs 6-9 are undefined and
rejected. Rules share the existing callback transaction: later failure rolls
back the rule together with money, inventory and other staged writes.

Add `apply_current=true` to apply the rule to the currently loaded floor too.
This requires ordinary native dungeon play and matching saved/live dungeon and
floor identity. Character restrictions require that character to be joined and
already active; use the party-switch API separately first. The call does not
load character resources, force a menu, or emit a restriction announcement.

These are explicit persistent save edits. Disabling the authoring mod does not
undo them; record and restore originals if temporary behavior is desired.
Existing native floor-entry logic reads the saved table. No generation, capacity,
per-frame rules override or default gameplay package is added.

### Owned monster loot definitions

### Owned dungeon layouts

`dungeon.layout_set {dungeon=0,rooms=64,area=46,room_min=3,room_max=6}`
(`game.write`) claims generation settings for future floor loads in one retail
dungeon (0–6). Rooms must be 2–64, area 8–46, and room sides 3–6 with
`room_min <= room_max`. Requested room count is a packing target, not a guarantee.
One package owns each dungeon's layout; a conflicting write reports its owner.
These writes participate in callback rollback.

An optional `chests=12` requests 2–24 generated treasure chests on each front
and back floor. Omit it to retain the retail random target. The first two front
floor chests retain native map/crystal assignment; other contents still use
retail loot selection. This does not increase the 24 chest slots: existing
generation chests also occupy slots, so a request may fail within the numeric
range. Exhaustion or bounded placement failure rejects the staged layout and
falls back to retail generation. Larger capacity and chest loot overrides
remain separate work.

An optional `monsters=5` requests 2–256 placement attempts on each floor
(at least three for Wise Owl Forest's three keys). One monster would bypass
native key assignment and is rejected.
Omission keeps native counts. The setting survives on the generated map for
back-floor entry and is cleared by map initialization. It applies only to
ordinary random-model placement with a positive native count; explicit-model
and zero-count calls retain their native behavior. Native model choice,
placement retry limits and key assignment remain in use, so the request is
not a guarantee that every attempted placement succeeds. Requests above 192
raise the nearby-count threshold from three to four within the native
480-unit radius; the native minimum separation remains unchanged. This density change applies
only with explicitly expanded active slots.
Requests above sixteen allocate extra scripts and activate the required monster,
hit and audio resources at floor load. Floors without an expanded request and
game exit restore retail active limits. No-mod floors keep sixteen active slots.
Read placement reports rather than assuming that a small layout can fit the
requested population.

`dungeon.layout_get {dungeon=0}` (`game.read`) returns `enabled`, `owner`,
`scope="future_floor_loads"`, `requested`, and `last_build`. The latter is
`{state="pending"}`, `{state="applied",floor,front_rooms,back_rooms,front_chests,back_chests}`, or
`{state="rejected",reason}`. Acceptance of the configuration does not guarantee
that a later floor can be built. Rejected preparation restores generation state
and falls back to native retail generation. Reported chest counts include
preexisting generation chests.

After native monster placement, an applied build also reports
`population_front` and, once visited, `population_back`. Each contains
`requested`, `placed`, `state` (`"applied"` or `"partial"`) and `reason`.
For example, `{requested=5,placed=3,state="partial",reason="native placement did not place every requested monster"}`
means the map was built but only three actors were added by that placement call.
This result is separate from layout generation success. Missing population
fields mean placement has not been reported; they do not mean zero monsters.
Reports are scoped to the matching applied claim, runtime epoch and floor.
An expanded population rejected by collision-resource preflight reports
`state="rejected"`, `fallback="native"`, its reason and `placed=null`. The
population override is cleared for that map and retail resource limits are
restored before native placement. Null does not claim a fallback placement
count. This handles resource-validation rejection only; allocation or native
setup exceptions propagate. Map-generation success is independent of this
population resource check.

`dungeon.layout_reset {dungeon=0}` (`game.write`) removes the caller's claim.
Owner retirement or runtime reset also removes claims. Existing loaded floors
are not regenerated; declare settings again in `on_game_enter` when needed.
Declare the operations used in the package's `requires_operations` manifest.

Expanded layouts use a 48×48 grid, with transport resources preflighted against
the supported retail programs. Unknown transport resources reject the request.
This API does not add monster types, extra floors, custom maps, items, weapons or
cutscenes. With no claims, the native generation path runs unchanged.

`dungeon.loot_get {monster=0}` (`game.read`) reads a retail monster definition
with ID 0-166. It returns `monster`, `rare_item`, `steal_item`, `attachment_kind`,
`drops_items`, per-field `owners`, `scope="future_model_loads"`, and live
`writable` readiness. Empty owner strings mean unclaimed fields. Monster IDs
identify definitions, not generation-checked live entity handles.

`dungeon.loot_set {monster=0, rare_item=177, steal_item=148}` (`game.write`)
sets repair powder as the rare drop and bread as the stolen item. Either field
may be omitted; at least one is required. Each field is owned separately, so
different packages can coexist on different fields. Same-field conflicts name
the monster, field and existing package owner. Set -1 to disable that item.

`dungeon.loot_reset {monster=0, field="rare_item"}` releases one field owned by
the caller. Reset and owner/runtime retirement restore the original only when
the current value still matches the owned value, preserving later external
changes. An unclaimed reset is a no-op; resetting another owner's field fails.
Writes require stable town walking or dungeon play outside events. Arm writes
through shared-manager actions and apply them after menus close. Loot writes
share callback rollback with inventory, currency, shops and entity commands.

Native definitions are copied when monster models load and again when actors
are created. Changes and restoration affect subsequent model loads; existing
model caches and actors retain their copied values. These overrides are not
save edits and must be reapplied by the mod after runtime reload. Reads within
a callback see committed state, not earlier writes still staged in that callback.

Values must resolve to existing native item, attachment or weapon definitions.
Synthesis spheres are rejected because their weapon-derived payload cannot be
represented by an item ID. This does not register contributor-owned custom
items/weapons. `attachment_kind` is the monster's combat family and is read-only;
`drops_items` is also read-only. Native rare-drop probability, steal checks,
forced drops and gate-key handling are unchanged. Chest loot is a separate API gap.

Native pickup semantics still apply. Full dungeon-item and attachment bags reject
pickups. The native weapon acceptance check does not reject a full weapon array;
when all eleven slots are occupied, the subsequent grant returns failure. A loot
definition override is not a guarantee that a generated pickup grants an item.
Authors should account for weapon capacity; this API does not change the retail
pickup path or overflow behavior. No ownership table entries, definition writes
or additional drop hooks exist without explicit calls.


## Owned chest loot pools

`dungeon.chest_loot_get {dungeon=0,back=false,kind="small"}` (`game.read`)
returns that selector plus `enabled`, `owner`, `entries`, and
`scope="future_chest_generation"`. Dungeon IDs are 0..6. `back` selects front
or back-floor generation; `kind` is `"small"` or `"weapon"`.

`dungeon.chest_loot_set` (`game.write`) takes the selector and a nonempty
`entries` array of 1..64 unique `{item,weight}` records. Weights are positive
integers up to 10000. Small pools accept valid retail items/attachments;
weapon pools accept valid retail weapon IDs. Synthesis spheres requiring a
payload are excluded. Example, using bread (148) and repair powder (177):

```lua
assert(chronicle.call("dungeon.chest_loot_set", {
  dungeon=0, back=false, kind="small",
  entries={{item=148,weight=1},{item=177,weight=3}}
}))
```

One package owns each selector. Other packages can own disjoint selectors;
conflicting claims and resets return the owner's ID. Writes participate in
callback transactions, so another failing command prevents pool publication.
Configuration can be declared in `on_game_enter`; it needs no loaded floor.
Declare the operations used in `requires_operations` and the corresponding
permissions in the package manifest.

`dungeon.chest_loot_reset` (`game.write`) takes the selector and releases only
the caller's pool. Runtime reset or owner retirement removes claims. Claims are
not persisted; declare them again each session. Already generated chest contents
are unchanged by setting, resetting or retiring a pool.

An active matching pool replaces random item selection with one weighted draw
from Chronicle's RNG. It bypasses native item rejection and held-weapon rate
adjustments for that selector. Native chest type, traps, placement, front-floor
map/crystal guarantees and separately placed door keys remain in use. Clown
rewards and other scripted rewards are outside this API. A pool does not
increase chest capacity. With no matching active claim, native selection and
RNG consumption are unchanged. No gameplay package is enabled by this API.


## Georama satisfaction readback

`georama.satisfaction_get {town=0}` (`game.read`) reports saved request
completion for a settlement (0 Norune, 1 Matataki, 2 Queens, 3 Muska Lacka,
4 Yellow Drops). It returns `town`, `request_count`, `completed_requests`,
`satisfaction_percent`, `requests`, and an explicit `derivation` description.
Each request has a zero-based `request` index and boolean `completed`.

The percentage is the nearest whole-number share of saved requests completed
(half rounds upward); an empty request list returns zero. Every nonzero native
completion flag counts as completed. Invalid saved counts outside 0..24 are
reported as errors rather than silently clipped. The query does not evaluate
current building placement, change completion flags, award rewards or run
native Georama scripts. Returned data is detached from the save. Use
`georama.get` for plot and placement details, and declare this operation in
`requires_operations` when using it from a package.


## Georama request completion

`georama.request_set {town=0,request=0,completed=true}` (`game.write`) changes
one saved native request flag. `town` is 0..5; `request` is zero-based 0..23
and must exist in that town's saved request table. Counts outside 0..24 and
non-boolean completion values are rejected. Declare the operation in
`requires_operations`.

The host requires stable town or dungeon play outside menus and transitions.
When the selected town is loaded, its request cache is updated in the same
transaction so the next native save preserves the change. Mismatched loaded
and saved request counts are rejected. Other towns update only their saved
flag. The whole callback rolls back if any command fails.

This is an explicit save change, not a persistent override or content registry:
disabling the mod does not undo it, and later native condition evaluation may
replace it. It does not run reward scripts, satisfy placement prerequisites,
change other flags, or add requests. Use `georama.get` or
`georama.satisfaction_get` to inspect the result.
