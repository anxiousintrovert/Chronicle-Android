# Chronicle mod framework — package and scripting API 1

This Chronicle-only framework loads asset replacements, bounded Lua gameplay
scripts, and custom quest definitions. It is separate from MoonFactory and uses
no MoonFactory runtime code. The vendored Lua 5.5.1 interpreter is MIT licensed;
see `port/third_party/lua/LUA-LICENSE.txt`. Optional native plugins require separate
local trust approval; see [the native SDK](MOD_NATIVE_SDK.md).
Implementation changes remain under `port/`; `ps2/` is unchanged. C and C++ builds
require the existing Clang toolchain.

Documentation: [manifest reference](MOD_FORMAT.md), [author walkthrough and Lua API](MOD_AUTHORING.md),
[troubleshooting](MOD_TROUBLESHOOTING.md), [host calls and author tools](MOD_HOST_API.md),
and [coverage and remaining work](MOD_PARITY.md).

The native manager uses Chronicle's shared `UI` menu framework for its window,
labels, list, selection brackets, and pointer. Lua packages can contribute
[named actions](MOD_HOST_API.md#mod-manager-actions) to this manager; arbitrary
script-drawn menus are not exposed.

Open **Options → Mods → Mod Manager**. Select a package with Up/Down, confirm to
toggle it, Left/Right to page through its details, Square to rescan, and Circle
to return. Keyboard bindings work through normal game controls. Mouse users can
select rows, click Toggle/Rescan/Back, and scroll the list or details pane.
The manager displays current-session activation separately from next-launch
status, plus conflicts, script errors, permissions, logs, and quest progress.
For native packages, Triangle or Trust opens a separate confirmation. Trust and
enabling are independent; both are needed to load native code after restart.
Packages with registered actions show **R1 Actions**. Open that list, select an
action, and press Cross (or click Run) to queue it for a gameplay tick. Circle
returns to the package list. The action pane shows readiness and the last result;
script failures also appear in package diagnostics. Actions belong to the current
run, even if you have changed the package's next-launch enable selection.

Packages with active manifest or dynamically registered quests show **L1 Quests**. Open the quest page,
use Up/Down or the mouse wheel to select an objective, and Left/Right to page its
description. Square or the Filter button cycles All, In progress, Completed, and Failed. Failed quests show their authored reason;
opening the page does not resume them.
Circle returns to the package list. This view reads existing progress without
advancing objectives or granting rewards; it uses the same shared UI framework.

Packages with registered skills show **L2 Skills** (or click Skills in the header).
This page shows ranks and missing prerequisites using the same controls and shared UI.
Cross or Train queues an owner-scoped training request when the mod registers a handler.
The framework does not automatically spend currency or change ranks.

Enable selections are written atomically to `<save>/mods-profile.json`; they do
not edit the distributed manifests. Conflicts are recomputed immediately.
**Restart to apply changes**. Rescanning never reloads live scripts or assets.
A corrupt profile blocks activation and is preserved for repair.

Full-resolution PNG replacements use the separate [texture package path](MOD_TEXTURES.md).
The same-size restriction below applies to raw `files` mappings.

## Install a mod

Put one unpacked package in either `<save directory>/mods/<package>/` or
`mods/<package>/` beside `darkcloud`. `--save DIR` also selects `DIR/mods`.
Create the `mods` directory if it does not exist. ZIP archives are not loaded by
the runtime; the [offline install tool](MOD_HOST_API.md#offline-installation-and-updates)
can validate and unpack them.
Directories beginning with a dot are ignored. Restart the game after changes.

```text
mods/
  author.title/
    mod.json
    assets/
      title.img
```

```json
{
  "schema_version": 1,
  "id": "author.title",
  "name": "My title artwork",
  "version": "1.0.0",
  "game": "chronicle",
  "requires": [],
  "conflicts": ["another.title"],
  "files": {
    "img/title.img": "assets/title.img"
  }
}
```

Supply your own valid Chronicle-format replacement. JSON alone does not convert
PNG, audio, or model files to game formats. Replacement files must have exactly
the original byte length: game code has fixed-size buffers. This check does not
validate the internal asset format. Metadata (`data.hd2`, `languages.json`) cannot
be replaced. Targets must already exist in the extracted data index. The loader
uses normalized game assets where the extraction supplies them.

`files` maps an ASCII game-relative path to a package-relative file. Case and slash
differences in target names do not avoid conflict detection. Absolute paths,
parent traversal, Windows alternate streams, and files resolving outside the
package are rejected. Do not modify packages while the game is running.

IDs begin with a lowercase letter and contain only lowercase ASCII letters,
digits, periods, underscores, and hyphens (maximum 96 characters). `version` is
checked exactly for gameplay saves. Optional `dependency_versions` supports
exact pins and SemVer constraints; see MOD_FORMAT.md.
`requires` and `conflicts` contain exact package IDs. Unknown manifest fields,
self references, repeated references, and duplicate JSON keys are rejected. At most
1024 packages are scanned. All fields shown above
are required except `requires` and `conflicts`. **Every new package is disabled.**
Only an explicit manager selection in the local profile enables it. The legacy
manifest `enabled` field is accepted but ignored, even when it is `true`.
An empty `files` object is permitted for script, quest, and dependency
grouping packages.

## Conflict rules

- Duplicate IDs block every copy, even across both installation locations.
- Two enabled valid packages replacing the same game file block both packages.
  There is no last-writer-wins policy and no implicit root priority.
- A `conflicts` declaration on either side blocks both enabled packages.
- Shared `exclusive` gameplay feature IDs also block both enabled packages.
- Missing, disabled, or blocked required mods block their dependents transitively.
- Dependency cycles block every participant and their dependents.
- Independent valid packages continue to load. An unreadable discovery root blocks
  the whole activation plan because the conflict scan is incomplete.

Dependencies load first, with deterministic ID ordering for independent packages.
Conflict analysis finishes before any replacement becomes visible. An invalid
package applies none of its files. Disabling a conflicting package and restarting
allows the remaining valid package to load. Dependency ordering never overrides a
file conflict.

The loader writes `<save>/mods-report.json` and prints diagnostics to stderr.
The report lists package directories, active/disabled/blocked state, reasons,
discovery errors, and the resolved order. It is generated output; editing it does
not change activation. Extracted game files are never modified. If a replacement
is missing or changes size after scanning, reads fall back to the original.

## Script and quest packages

Copy the supplied `examples/mods/norune-welcome` folder into your save's `mods`
directory for a working example. It registers a quest to visit Norune, grants
50 Gilda once, and displays progress in the manager. Test on an independent save.
The original game's quest definitions and story scripts are not replaced.

Additional optional manifest fields:

```json
{
  "api_version": 1,
  "script": "main.lua",
  "permissions": ["economy.write"],
  "requires_capabilities": ["quests.progress.v1", "save.metadata.v1"],
  "exclusive": ["economy.reward-policy"],
  "quests": [
    {"id": "visit", "title": "Return to Norune", "description": "Visit the town.", "goal": 1, "map": 0}
  ]
}
```

These are fields to add to a complete manifest, not a standalone manifest.
Scripts require `api_version: 1` and a package-contained UTF-8 `.lua` file of at
most 1 MiB. Each script returns a table with optional `on_tick(world)` and
`on_map_enter(world)` functions. `world` contains numeric `mode`, `map`, `money`
and boolean `playing`. Tick callbacks run on game simulation steps, not render
interpolation. They run only in town/dungeon gameplay. Map callbacks also run on
the first gameplay observation after loading. Built-in visit objectives remember
the previous map in the save, so loading in the same town does not add a visit.

| Lua operation | Contract |
|---|---|
| `chronicle.api_version` | Integer API major, currently 1 |
| `chronicle.supports(name)` | Whether the host implements a named capability |
| `chronicle.get(key)` | Read this mod's scalar save-bound value; nil when absent |
| `chronicle.set(key, value)` | Store boolean, finite number, or string; nil deletes |
| `chronicle.quest_get(id)` | Read this mod's declared quest progress |
| `chronicle.quest_add(id, amount)` | Add 0–1,000,000 during gameplay, capped at the goal |
| `chronicle.add_money(delta)` | Queue a bounded currency delta; requires `economy.write` |

State keys use lowercase ASCII letters, digits, dots and underscores, at most
64 bytes. Each mod gets 128 scalar keys; strings are at most 256 bytes. Each
package may define 64 quests with goals from 1 to 1,000,000. Omit `map` for a
script-managed objective. IDs are local to their package. Quest titles and
descriptions appear in the manager; there is not yet a separate native journal
tab. For complete capability and operation contracts, see [the host API](MOD_HOST_API.md)
and [package format](MOD_FORMAT.md). Unknown required capabilities block activation.
Supported permissions are `game.read`, `game.write`, `economy.write`,
`resources.read`, and `native.extension`; request only those used by the package.
Dynamic quests support named objectives, explicit start, event bindings and
transactional rewards. Register them through host calls rather than adding fields
to the simpler manifest quest declarations.

Each Lua state is limited to 8 MiB and each chunk/callback to 100,000 Lua
instructions. No OS, filesystem, native loader, debug, coroutine, dynamic-code
loader, protected-call, or metatable API is exposed. Limits contain ordinary
script failures; they are not a claim of a hardened security boundary for hostile
code or a wall-clock deadline for Lua's C library functions.

Script errors stop that script and its dependent scripts, while independent mods
continue. Per-callback state writes and currency requests roll back on callback
failure. Asset replacements from that package remain fixed for the session.
Currency is clamped to the native 0–65,535 range; deltas are limited to +/-32,767
per callback. Scripts that manually grant rewards must store guards with `chronicle.set` to avoid
granting a reward every tick. Prefer dynamic quest reward declarations when they
fit: completion and the one-time receipt commit together and persist in the same
save. See [declarative quest rewards](MOD_HOST_API.md#declarative-quest-rewards). No script receives raw native pointers or offsets.

## Save compatibility

Gameplay mod state is appended after the unchanged native save image in a
versioned extension, with a CRC32 over both sections. Native data and mod data
are committed together using the existing atomic file writer, for manual saves
and autosaves. An unmodded save remains readable and starts fresh mod progress.
New-game initialization resets mod state and Lua globals.

Extended saves require working enabled gameplay mods at their recorded versions,
or an explicit [direct migration](MOD_FORMAT.md#save-migrations) declared by the
new package. Missing, disabled, failed, or incompatible required mods fail before the
active native save is replaced. Corrupt extensions are rejected. The diagnostic
is printed to stderr; the game's load screen uses its existing failure message.
Earlier Chronicle builds may ignore the extension and drop it when saving, so
do not round-trip a modded save through an older build. Other save extensions,
including the separate NG+ prototype trailer, have no migration path yet and
are explicitly rejected rather than silently discarded.

## Current boundaries and intended scope

Conflict detection covers declared incompatibilities, dependency constraints,
exact file ownership, and matching mesh/texture replacement targets.
It cannot infer semantic conflicts between different game assets or undeclared
script interactions. The offline author CLI can install or replace packaged ZIPs with backups. Dependency version constraints are supported through `dependency_versions`; see
[the package format](MOD_FORMAT.md) for syntax. The runtime has no hot reload,
foreign native-plugin ABI compatibility, or importer for other frameworks' quest
formats. Save compatibility is not certified by a successful
package scan.

The goal is a general framework supporting substantial gameplay mods such as the
separate NG+ project. See [host capability scope](MOD_FRAMEWORK_SCOPE.md) for the
required native integrations and their acceptance criteria. Selected-save
branching, native save-list extensions, inventory/weapon/party transfer,
and recruitment lifecycle hooks remain outside the completed scope. Owned enemy
stat modifiers and damage rules are available; their contracts and validation
boundaries are documented in [the coverage ledger](MOD_PARITY.md). Do not claim that the NG+ package can run yet.
Raw native asset replacements remain the same byte length. Full-resolution PNG
texture replacements use a separate upload path that preserves native logical
coordinates; see [texture authoring](MOD_TEXTURES.md) for discovery, target hashes,
limits, conflicts and fallback. Other larger native assets still require
caller-specific buffer knowledge.

## Validation

Build the `darkcloud` and `darkcloud_tests` targets with the project's Clang PC
toolchain. Run `ctest --test-dir <build> -R 'ModManager|ModRuntime|DataRead|SaveSlots|MenuOption|Localize|IntegrationGameloop' --output-on-failure`.
The manager and script tests use synthetic files. DataRead tests exercise actual port file
reads, sector padding, conflict fallback, and changed-size fallback. They do not
establish that an authored replacement asset renders correctly in-game.
