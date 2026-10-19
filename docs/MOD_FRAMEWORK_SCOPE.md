# General Chronicle framework: host capability scope

A substantial New Game Plus (NG+) mod is a target acceptance case for this
framework. The services below describe the additional host integration needed
to support it; API 1 does not yet run a full NG+ package.
The Chronicle-only framework must allow separately distributed packages to use
stable host APIs instead of depending on save layouts, C++ symbols, function
addresses, or copied PS2 internals.

## Implemented and available

- Folder packages, manifest validation, dependencies, duplicates and file conflicts.
- Declared incompatibilities and exclusive gameplay-feature claims.
- In-game enable/disable manager, atomic local profile, restart-only activation.
- Required-capability negotiation; unsupported operations block activation.
- Bounded Lua callbacks for simulation ticks and map entry.
- Owned custom quests, progress display, and permission-checked currency rewards.
- Namespaced per-save scalar state; single-file atomic native/mod serialization,
  checksummed extension, corruption rejection, and missing-mod/version checks.
- Lazy player/party/inventory/weapon/world snapshots and transactional player
  health/water/currency calls; see [the exact host operation inventory](MOD_HOST_API.md).
- Bounded logs and package-owned resource reads; offline create/check/inspect/
  check-set/pack tools, operation and capability discovery.
- Explicitly enabled and separately trusted native C ABI plugins with the same
  host calls, logging and simulation callbacks. Windows loader tested; other
  native platforms require validation. See [the native SDK](MOD_NATIVE_SDK.md).

This is **not full MoonMod capability parity**. [The coverage ledger](MOD_PARITY.md)
separates implemented services from the remaining native integration work.

## Native services still required for NG+ and larger mods

| Service | Required behavior | Acceptance evidence before exposing it |
|---|---|---|
| Save inspection and branching | Inspect a selected save through an opaque content-bound token; prepare a fresh run offline; confirm and commit to an independent slot without touching the source | Cancellation, stale source, failed write, repeated branches, protected source slot, autosave and copy/import tests |
| Carryover policy | Explicit typed transfer of all six characters, HP/defense/thirst, weapons/stats/attachments, inventories/storage, currency, fishing points/equipment; filter story-critical objects | Native reference fixtures and inventory conservation, no duplicated starter weapons or story keys |
| Story and world reset | Fresh story, dungeon/boss flags, town reconstruction, Georama placement, NPC flags, and story items; configurable tutorial policy | Playthrough checkpoints and intro events verified across every region |
| Recruitment lifecycle | Preserve introduction scenes while retaining companion equipment/stats until normal recruitment | All five introductions and all six character loadouts verified |
| Owned enemy modifiers | Apply before combat/AI and boss percentage-HP initialization, once per actor generation; immutable base stats, boss phase/type context | Actor-slot reuse, phase resets, disabled mods, overflow limits, no compounding or template mutation |
| Damage context | Explicit enemy identity, melee/projectile/hazard/percentage category; deterministic modifier composition | Six companion baselines, boss vulnerabilities, shields and hazards retain intended behavior |
| Combat-mod composition | Named stages and bounded owned operations with deterministic ordering; reject incompatible replacement policies | NG+ plus a Dungeon+ style package has documented arithmetic and conflict behavior |
| Save-menu contributions | Owned selected-save actions with confirmation and bounded suffix providers, e.g. + / ++ / +++ / +N | Native menu navigation, cancellation, labels before loading, localization and controller/mouse tests |
| Registered content | Stable IDs and ownership for items, weapons, enemies, dungeons, shops, dialogue, quest events and Georama pieces | Content survives save/reload and reports missing owners; caller allocation limits verified |
| Save schema migration | Per-mod schema versions and explicit migrations, including older NG+ trailers; retain source backups and fail transactionally | Old versions, corruption, missing migrator, downgrade and rollback fixtures |

Names such as `save.branch.v1`, `combat.modifiers.v1`, `party.recruitment.v1` and
`ui.save_actions.v1` are reserved design proposals, **not supported capabilities**.
Packages requiring them must stay blocked until the native implementation and
tests exist. API-major compatibility alone must never imply every domain exists.

## Quest and other-mod import boundaries

API 1 accepts Chronicle-framework quest manifests and Lua scripts. A quest mod
can register objectives and rewards through those interfaces. Importing another
framework's Lua, binary patch, ISO mod, model pack, dialogue tree, or quest format
requires an explicit adapter/converter and validation of every operation it uses.
Do not rename a foreign manifest and claim compatibility.

Broader asset support also needs format-specific loaders and native allocation
contracts. The current asset mapping accepts more than textures (it can replace
existing compatible audio/model/data assets), but same-length files are not
necessarily structurally valid. Arbitrary new playable characters, maps, rigging,
animations, and combat systems are not supplied by the generic folder loader.

## Contribution boundary

Use existing Clang and the port override mechanism. Never edit or generate into
`ps2/`. Keep renderer/timing changes out of mod runtime work. Implement native
services as independently testable adapters, then expose versioned capabilities.
Retain the native NG+ implementation until a real loadable NG+ package passes its
existing save, carryover, recruitment and combat regressions plus full gameplay
testing. Compile success and package discovery do not establish compatibility.
