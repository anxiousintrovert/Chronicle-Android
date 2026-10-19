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
| `quests` | No | Array, `[]` | Up to 64 quest definitions, described below |
| `resources` | No | Object, `{}` | Up to 16 resource ID → contained relative file mappings; each file at most 64 KiB |
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
| `assets.replace.v1` | Existing same-size game asset replacement |
| `scripts.lua.v1` | Isolated Lua script runtime |
| `quests.progress.v1` | Declared objectives and owned progress |
| `save.metadata.v1` | Namespaced state attached to native saves |
| `economy.write.v1` | Bounded currency changes; additionally needs permission |
| `events.map.v1` | Map-entry callback and built-in visit tracking |
| `events.tick.v1` | Gameplay simulation callback |
| `player.read.v1`, `party.read.v1`, `inventory.read.v1` | Lazy, detached legacy snapshots; require `game.read` |
| `events.lifecycle.v1` | Owned Lua listeners for tick, map/floor entry, gameplay enter/leave, and deferred save-loaded notification |
| `services.query.v1` | Declared-dependency Lua query exports; bounded structured requests/results; no managed writes |
| `resources.read.v1` | Owned startup-cached package resources; requires `resources.read` |
| `logging.v1` | Bounded per-session diagnostics |
| `host.calls.v1` | Catalogued, permission-checked native queries and transactional writes |
| `plugins.native.v1` | Opt-in Chronicle C ABI 1 plugins; not a MoonMod ABI compatibility claim |

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
