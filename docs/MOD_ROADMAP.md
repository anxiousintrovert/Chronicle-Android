# Chronicle mod framework roadmap

This roadmap records planned work, not capabilities that packages can use today.
There are no committed release dates. Use the [host API](MOD_HOST_API.md) and
[package format](MOD_FORMAT.md) for implemented contracts; the
[coverage ledger](MOD_PARITY.md) records detailed evidence and remaining gaps.

## First release

The initial scope is quests, same-size native asset replacement, full-resolution
PNG texture replacement and reliable installation/management tools.

Implemented foundations include disabled-by-default folder packages, dependency
and conflict checks, the shared mod manager, texture discovery/export/package
creation, and quest author templates. Quests support named and ordered objectives,
explicit start, failure/resume, native and package-owned event bindings, one-time rewards and
owned save state. Full-resolution texture testing includes a 4096x4096 replacement.
These capabilities have documented limits; implementation is not universal
platform or full-playthrough certification.

Before declaring the first release ready, finish representative end-to-end quest
playthrough checks, broaden palette-driven/texture-copy scenario validation, and
verify supported platforms beyond the current Windows evidence. Keep the author
walkthroughs and diagnostic messages aligned with those results.

## Planned expansions

| Area | Planned work |
|---|---|
| Quests and dialogue | Broader stage and branching lifecycle, dialogue trees, pickup and Georama objective events, and additional journal services |
| Skills and player systems | Additional skill effects, broader player/party events and tuning, and richer character-sheet integration |
| World content | Registered maps, characters, models and animations; broader floor transitions, rules and content limits where explicitly requested by a mod |
| Items and combat | Broader retail item effects, live weapon/model transitions, more actor lifecycle events and combat scenarios |
| Towns and Georama | Additional NPC/interior services and structure registration with explicit ownership and lifecycle rules |
| Rendering and audio | Owned overlays, additional camera/audio controls and broader lighting scenarios |
| Author tools | Content editors/converters, richer runtime inspection, more templates and repeatable scenario testing |
| Plugins | Broader host services and lifecycle events, plus Linux/macOS/Android validation of the native SDK |
| Saves and new runs | Selected-save inspection, independent save branching, typed carryover policies and explicit world-reset services for separately distributed new-run mods |
| Package management | Evaluate remote discovery and update tooling after local install/update and recovery workflows are established |

Some expansions require engine and platform research before their exact API or
format can be committed. Unsupported features must remain unavailable rather
than appearing to succeed without an implementation.

## Contributor-owned extensions

Custom item, custom weapon and cutscene frameworks are planned as separate
contributor work. This roadmap does not assign their registries, file formats or
implementation to the core framework. Keep host services extensible so those
contributions can integrate without rewriting the loader or bypassing ownership,
permissions, saves and the shared menu framework.

## Rules for every addition

- With no participating mods, preserve native engine behavior. New capacities,
  policies and hooks must not silently become gameplay defaults.
- Keep gameplay mods separate from the framework and disabled by default.
- Use the shared menu framework and the upstream renderer; build with Clang.
- Leave every PS2 directory read-only.
- Document ownership, conflicts, failure/cleanup behavior and save compatibility.
- Ship APIs with relevant regression and gameplay evidence, clearly stating what
  was tested and which platforms or scenarios remain unverified.
