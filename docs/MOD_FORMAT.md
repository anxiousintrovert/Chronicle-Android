# Manifest reference: schema 1 / API 1

Start with [the guide](MODS.md) or [the author walkthrough](MOD_AUTHORING.md).
Every package is a directory containing exactly one root `mod.json`. Use UTF-8
JSON, double-quoted strings, and no comments or trailing commas. The manifest
must be an object, no larger than 256 KiB. Unknown fields and duplicate keys
(including nested keys) are errors. All string lengths below are UTF-8 **bytes**.
Do not add `$schema`, `author`, `description`, or arbitrary metadata fields to
schema 1; put release notes and attribution in a separate README.

## Complete field inventory

| Field | Required | Type / default | Rules |
|---|---|---|---|
| `schema_version` | Yes | Integer | Exactly `1` |
| `id` | Yes | String | 1–96 bytes; `[a-z][a-z0-9._-]*`; globally unique; keep stable across releases |
| `name` | Yes | String | 1–256 bytes, no control characters; manager display name |
| `version` | Yes | String | 1–256 bytes, no control characters; recommend `1.0.0`; exact string checked by gameplay saves; SemVer precedence only for explicit dependency ranges |
| `game` | Yes | String | Exactly `chronicle` |
| `files` | Yes | Object | Up to 4,096 game-target → package-source string mappings; `{}` is valid |
| `enabled` | No | Legacy Boolean | Validated but ignored; every newly discovered package is disabled |
| `requires` | No | String array, `[]` | Up to 128 unique package IDs; no self dependency; all must be enabled and valid |
| `dependency_versions` | No | Object, `{}` | Up to 128 declared dependency IDs mapped to exact pins or SemVer bounds; see below |
| `conflicts` | No | String array, `[]` | Up to 128 unique package IDs; no self reference; either side's declaration blocks both |
| `exclusive` | No | String array, `[]` | Up to 128 unique feature IDs with the same syntax as package IDs; two owners conflict |
| `requires_capabilities` | No | String array, `[]` | Up to 128 unique capability IDs; every capability must be implemented by the host |
| `api_version` | For code | Integer | Exactly `1`; required for `script` or `native_library` |
| `script` | No | String | Relative path to a contained, regular `.lua` file, at most 1 MiB; text source only |
| `permissions` | No | String array, `[]` | `economy.write`, `game.read`, `game.write`, `resources.read`, `native.extension`; no duplicates |
| `save_migrations` | No | Array, `[]` | Up to 16 direct old-version migrations for owned state; see [save migrations](#save-migrations) |
| `quests` | No | Array, `[]` | Up to 64 quest definitions, described below |
| `resources` | No | Object, `{}` | Up to 16 resource ID → contained relative file mappings; each file at most 64 KiB |
| `audio` | No | Object, `{}` | Up to 16 local IDs (`[a-z][a-z0-9._-]*`, at most 64 bytes) mapped to contained WAV/FLAC/Ogg Vorbis paths; nonempty, at most 32 MiB each and 64 MiB encoded per package; see audio support below |
| `textures` | No | Array, `[]` | Up to 256 name/content-hash targets mapped to bounded full-resolution PNGs; see MOD_TEXTURES.md |
| `meshes` | No | Array, `[]` | Up to 16 existing-model replacements, 16 MiB each/64 MiB total. Requires explicit enable and restart. See [mesh declarations](MOD_MESH_FORMAT.md#package-declarations). |
| `native_library` | No | Relative path | Contained `.dll`, `.so`, or `.dylib`, 1 byte–64 MiB; requires `native.extension` and separate local trust |

Do not invent fields such as `load_order`, `priority`, `entrypoint`, `rewards`,
`dependencies`, or `min_version`. They are not aliases. Dependency ordering is
automatic; file conflicts cannot be resolved by ordering.

### Quest objects

| Field | Required | Rules |
|---|---|---|
| `id` | Yes | Same ID syntax as a package; unique within this package |
| `title` | Yes | 1–256 bytes, no control characters |
| `description` | Yes | 1–256 bytes, no control characters |
| `goal` | Yes | Integer 1–1,000,000 |
| `map` | No | Integer 0–999; adds one visit on entry to this map, capped at goal |

Omit `map` for a script-controlled objective. Map numbers are native Chronicle
IDs, not names or new map definitions. `0` is used for Norune in the working
example. The accepted numeric range does not mean all those maps exist.
No other quest fields are accepted. Rewards belong in Lua, not quest JSON.
Quests are owned by their package: scripts cannot update another mod's quests.

### File paths

Paths are 1–240 bytes, relative, and may use `/` or `\`; prefer `/` in JSON.
No empty components, `.`/`..`, absolute paths, control characters, colon,
`* ? " < > |`, or components ending in a space or period. Sources must resolve
to regular files within the package. Game targets must be ASCII; matching folds
ASCII case and separators. `data.hd2`, `languages.json`, and `normalized/` targets
are reserved extraction metadata. Use the original logical game asset path,
not the extractor's normalized output path.

The target must already exist, and the replacement must be nonempty and exactly
the same byte size. Size compatibility is not format validation. Arbitrary PNG,
MP3, glTF, or foreign quest files cannot be installed by renaming their extensions.

## Complete gameplay manifest

This is the format of a standalone, installable package; ship `main.lua` beside it.

```json
{
  "schema_version": 1,
  "api_version": 1,
  "id": "example.norune-welcome",
  "name": "Norune Welcome Quest",
  "version": "1.0.0",
  "game": "chronicle",
  "requires": [],
  "conflicts": [],
  "exclusive": [],
  "requires_capabilities": [
    "scripts.lua.v1", "quests.progress.v1", "events.map.v1",
    "events.tick.v1", "save.metadata.v1", "economy.write.v1"
  ],
  "permissions": ["economy.write"],
  "script": "main.lua",
  "quests": [
    {
      "id": "welcome",
      "title": "Welcome to Norune",
      "description": "Visit Norune. Receive 50 Gilda once per playthrough.",
      "goal": 1,
      "map": 0
    }
  ],
  "files": {}
}
```

## Capability inventory

| Capability | Host service |
|---|---|
| `assets.textures.v1` | Full-resolution PNG replacement of named original texture content |
| `assets.replace.v1` | Existing same-size game asset replacement |
| `skills.training.v1` | Owner-scoped deferred shared-menu training requests |
| `skills.defense.v1` | Explicit trained-rank reduction of dungeon native life deductions |
| `skills.effects.v1` | Explicit trained-rank outgoing enemy damage bonuses |
| `skills.trees.v1` | Owned skill definitions, prerequisites, ranks and shared-manager browsing |
| `quests.events.v1` | Owned objectives driven by existing map/floor/death events |
| `quests.rewards.v1` | Transactional one-time quest currency/item/weapon rewards |
| `quests.lifecycle.v1` | Opt-in explicit quest start and registered/active/completed status |
| `quests.objectives.v1` | Dynamic quests with up to 16 independently tracked named objectives |
| `quests.dynamic.v1` | Transactional registration and progression of owned saved objectives |
| `quests.read.v1` | Shared Lua/native owner-scoped quest metadata and progress queries |
| `save.migrations.v1` | Explicit declarative migration of package-owned saved values and quest IDs |
| `assets.mesh_replace.v1` | Declarative existing-model geometry replacement with inherited native animation/materials |
| `scripts.lua.v1` | Isolated Lua script runtime |
| `quests.progress.v1` | Declared objectives and owned progress |
| `save.metadata.v1` | Namespaced state attached to native saves |
| `economy.write.v1` | Bounded currency changes; additionally needs permission |
| `events.map.v1` | Map-entry callback and built-in visit tracking |
| `events.tick.v1` | Gameplay simulation callback |
| `player.read.v1`, `party.read.v1`, `inventory.read.v1` | Lazy, detached legacy snapshots; require `game.read` |
| `events.lifecycle.v1` | Owned Lua listeners for tick, map/floor entry, gameplay enter/leave, and deferred save-loaded notification |
| `events.custom.v1` | Deferred package-owned Lua/native events; dependency-scoped subscriptions and transactional publication |
| `services.command.v1` | Explicit Lua/native command exports callable by Lua/native dependents; one shared managed/game transaction |
| `services.query.v1` | Declared-dependency Lua/native query exports; bounded structured requests/results; no managed writes |
| `menu.actions.v1` | Owned Lua actions in the shared mod manager; explicit user invocation queues a normal gameplay transaction |
| `menu.native.v1` | Optional ABI 2 native menu descriptor; actions share the manager, limits and transaction rules with Lua actions |
| `resources.read.v1` | Owned startup-cached package resources; requires `resources.read` |
| `logging.v1` | Bounded per-session diagnostics |
| `combat.rules.v1` | Owned deterministic incoming/outgoing damage formulas through shared host calls |
| `entities.spawn_tuning.v1` | Optional bounded stats and initial rotation for owned loaded-model spawning |
| `host.calls.v1` | Catalogued, permission-checked native queries and transactional writes |
| `plugins.native.v1` | Opt-in Chronicle C ABI 1 plugins; foreign plugin ABIs are not supported |
| `plugins.native.v2` | Separate C ABI 2 with owned lifecycle subscriptions and scalar save values; ABI 1 remains supported |

List the capabilities your package actually needs. `chronicle.supports(name)`
allows optional behavior; it does not grant permissions. Unsupported required
capabilities block the entire package. See [planned services](MOD_FRAMEWORK_SCOPE.md)
for operations that are not available yet.

## Required host operations

Optional `requires_operations` lists exact implemented operation names (at most
128 unique IDs), for example `"requires_operations": ["story.flag_get", "player.heal"]`.
Declare their permissions too: `"permissions": ["game.read", "game.write"]`.
The game and offline validator reject missing operations or missing permissions
before executing package code. This does not guarantee that a runtime character,
weapon, gameplay phase, or target flag is appropriate; calls still validate those.
Use `chronicle.api_available` for optional features instead.

## Dependency version requirements

`requires` continues to list package IDs. Optional `dependency_versions` constrains
only IDs already in `requires`; it cannot introduce a hidden dependency.

```json
{
  "requires": ["author.shared-api", "author.legacy"],
  "dependency_versions": {
    "author.shared-api": {"minimum": "1.2.0", "before": "2.0.0"},
    "author.legacy": {"exact": "legacy-v1"}
  }
}
```

`exact` compares the complete version string, including build metadata, and also
supports legacy labels such as `1`. It cannot be combined with range bounds.
`minimum` is inclusive; `before` is exclusive; either or both may be present.
Bounds must use full SemVer (`major.minor.patch`, optional prerelease/build).
Numeric identifiers cannot have leading zeroes. Range precedence ignores build
metadata, compares numeric identifiers numerically without integer overflow, and
orders prereleases before their release. Prereleases participate directly: for
example `1.9.0-beta` satisfies `>=1.2.0 <2.0.0`; no npm-style prerelease exclusion
is applied. An installed non-SemVer label cannot satisfy a range.

Empty, reversed or unknown bounds fail discovery. A mismatch blocks the consumer
and its dependents, not the provider. The menu shows the constraint and installed
version. The CLI uses the same rules. This is local validation, not a downloader
or solver that installs another version. Save-state version matching remains exact.

Capability `services.native.v1` enables optional ABI 2 native query/command
exports with the same authorization and transactions as Lua services.

## Audio asset declarations

```json
"audio": {
  "notification": "sounds/notification.wav",
  "ambience": "sounds/ambience.ogg"
}
```

These named package assets are distinct from game-file replacements and the
64 KiB script-resource cache. Check/pack/install tools include and validate them.
Manifest scanning checks containment, extension and encoded sizes; it does not
prove successful decoding. Newly installed packages remain disabled.

The internal loader takes an atomic decoded snapshot of an active package:
all assets succeed or none are registered. It rechecks containment and sizes,
checks aggregate PCM admission before each decode, and does not read inactive
packages. PCM limits are 32 MiB per track, 64 MiB per owner and 128 MiB globally;
at most 16 sounds per owner and 64 globally. Decoders accept 8..192 kHz and
1..8 source channels, duplicating mono or retaining the front stereo pair.

Active code packages with audio require `resources.read`; startup decodes their
assets before running Lua/native initialization. A failure stops that package
and appears in its diagnostics. Declaring audio does not start playback.
`audio.state` requires `game.read`; `audio.play` and `audio.stop` require
`game.write`. Use `requires_operations` to require these operations. See the
[audio host API](MOD_HOST_API.md#owned-audio-playback).


## Save migrations

Exact package versions remain the default requirement for a modded save. A new
release may explicitly accept an old version with `save_migrations`; require
`save.migrations.v1` when using it. Each rule migrates directly from one exact
`from` string to this manifest's current `version`, with no inferred version
ranges, chaining, automatic downgrades or removal of missing packages.

```json
"save_migrations": [
  {
    "from": "1.0.0",
    "rename_values": {"old_counter": "counter"},
    "rename_quests": {"old.quest": "new.quest"},
    "remove_values": ["obsolete_cache"],
    "defaults": {"introduced_flag": false}
  }
]
```

There are at most 16 rules, each with a unique old version different from the
current version. Only `from` is required. A rule containing only `from` explicitly
asserts unchanged owned-state compatibility for that release. Unknown fields and
invalid declarations block package discovery, even when the package is disabled.

Value keys use `[a-z0-9._]{1,64}`; quest IDs use the normal quest/package ID syntax.
Rename maps have unique destinations and at most 128 value or 64 quest entries.
Renames are simultaneous (swaps are supported), ignore absent source keys and
reject collisions with existing unrelated destinations. Removals are bounded to
128 distinct value keys and cannot overlap either side of a value rename.
Defaults are up to 128 boolean, finite-number or at-most-256-byte string values;
they only fill absent keys after renaming/removal and cannot recreate a removed
key. Quest progress is preserved, not clamped or reset. Dynamic quest definitions move
with their progress, named-objective counters, explicit-start records and reward receipts when `rename_quests` is used. Include any corresponding
reward-granted flags in the migration design; declarative receipts migrate automatically. The framework does not infer
semantics for author-managed reward flags or replay rewards on migration.

Loading validates old state, applies each required owner's declared transform to
a detached copy, validates the resulting state and initializes the new runtime
before publishing it. Any failure leaves current state and runtime epoch intact.
The existing 128-value/64-quest per-owner and 4 MiB save limits still apply.
Migrations have no script callbacks, engine writes, file access or access to
another package's state. Normal `save_loaded` notification follows successful
loading. New saves record the current package versions; loading alone does not
rewrite the source save. Keep backups and test every accepted old version.

This supports owned scalar/quest schema changes. Native inventory/world save
changes, arbitrary scripted transforms, save branching and full NG+ migration
are separate unfinished work. Keep packages disabled by default and use the
existing shared manager to enable them before loading dependent saves.
