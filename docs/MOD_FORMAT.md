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
| `version` | Yes | String | 1–256 bytes, no control characters; recommend `1.0.0`; exact string checked by gameplay saves, not a semantic-version resolver |
| `game` | Yes | String | Exactly `chronicle` |
| `files` | Yes | Object | Up to 4,096 game-target → package-source string mappings; `{}` is valid |
| `enabled` | No | Boolean, `true` | Initial preference; saved profile wins |
| `requires` | No | String array, `[]` | Up to 128 unique package IDs; no self dependency; all must be enabled and valid |
| `conflicts` | No | String array, `[]` | Up to 128 unique package IDs; no self reference; either side's declaration blocks both |
| `exclusive` | No | String array, `[]` | Up to 128 unique feature IDs with the same syntax as package IDs; two owners conflict |
| `requires_capabilities` | No | String array, `[]` | Up to 128 unique capability IDs; every capability must be implemented by the host |
| `api_version` | For scripts | Number | Use integer `1`; required when `script` is present |
| `script` | No | String | Relative path to a contained, regular `.lua` file, at most 1 MiB; text source only |
| `permissions` | No | String array, `[]` | Only `economy.write` is currently recognized; no duplicates |
| `quests` | No | Array, `[]` | Up to 64 quest definitions, described below |

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
  "enabled": true,
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

List the capabilities your package actually needs. `chronicle.supports(name)`
allows optional behavior; it does not grant permissions. Unsupported required
capabilities block the entire package. See [planned services](MOD_FRAMEWORK_SCOPE.md)
for operations that are not available yet.
