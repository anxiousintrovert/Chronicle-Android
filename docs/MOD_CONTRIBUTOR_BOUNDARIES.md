# Integration boundaries for contributor frameworks

Cutscene support, custom item registration and custom weapon registration are
reserved for the contributor. This framework does **not** implement those three
systems, allocate their IDs, claim their manifest sections, or prescribe their
content formats. They are external integration work, not missing systems to
silently replace with competing implementations.

## Stable core to reuse

- `port/src/mods/api.hpp`: operation metadata, permission checks and actual
  availability. Add implemented operations to the catalog only when their native
  binding exists. Packages can declare `requires_operations`; unavailable methods
  produce manager diagnostics before scripts or plugins run.
- `port/src/mods/arguments.hpp`: domain-independent structured JSON transport for
  Lua and native host calls. Namespaced string identities and nested data can be
  carried without changing the native C ABI. Per-operation validation still owns
  the content schema. Lua has no arbitrary native pointer access.
- `port/src/mods/runtime.hpp`: common query/queued-command boundary. A successful
  callback commits its writes once; errors discard them and restore owned script
  state. Save-field transactions currently stage copies. A future cutscene or
  resource-loading command must not perform irreversible/global side effects
  during validation or pretend those effects are rolled back by copying a save.
  Queued `GameCommand.owner` is assigned by the runtime for both Lua and native
  callers; future domains can use it for ownership without trusting a caller's
  JSON identity. The current enemy-stat adapter owns only live retail fields;
  its forced-drop selector does not allocate or reserve custom item identities.
- `services.command.v1` and `events.custom.v1`: generic dependency commands and
  deferred owned notifications are available without defining contributor content
  formats. A Lua command export receives the runtime-assigned caller identity and
  shares the consumer's transaction. Provider permissions and engine ownership
  apply; irreversible resource/cutscene work still needs a staged commit design.
- `port/src/mods/manager.hpp`: package discovery, explicit local enable selection,
  dependency order, permissions, claims and diagnostics. Custom content should
  remain in separate packages, disabled by default. The existing parser rejects
  unknown fields; adding an agreed manifest extension requires changing both the
  runtime parser and offline author validator in the same integration.
- `port/src/mods/menu.cpp`: existing Chronicle UI windows/lists/cursors. Future
  author menus should use the shared UI/input lifecycle, including cancellation,
  restart semantics and visible failure reasons.
- `port/src/mods/native_plugin.h`: versioned C host boundary. ABI 2 adds observed
  lifecycle notifications and package-owned scalar save values without engine
  pointers, custom content registries or reserved content IDs. These generic
  notifications do not define cutscene, item or weapon events. Resource ownership,
  scene-ready hooks and structured content persistence remain decisions for the
  contributor's integration; the scalar store does not mandate their save format.

## Native inventory versus custom content

Existing numeric `item`, `character`, and weapon `slot` values refer to retail
Chronicle data and ephemeral native slots. They are **not** a global custom-content
identity space and must not be serialized as stable identities for registered
content. Native range checks belong in the retail adapter, not generic argument
transport. A future contributor can expose namespaced string IDs through distinct
operations or explicitly versioned selectors without changing existing meanings.
Do not preallocate a custom ID range, enlarge native tables, or replace retail
lookup rules merely to anticipate custom items or weapons.

The current save APIs mutate persistent retail fields when explicitly requested.
Temporary equipment overlays, cutscene ownership, and custom-content lifecycle
need their own acquire/release rules. They must not inherit an implicit promise
that disabling a package undoes earlier saved gameplay changes.

## Integration acceptance

The contributor chooses the content schema and registration API in coordination
with the project; none is implemented or frozen by this document. Integration
must preserve the existing no-mod early return and retail capacities, use Clang,
keep changes outside `ps2/`, and use the menu framework for UI. New engine hooks
must remain dormant without owners. Tests must cover enabled behavior, missing
content, failed callbacks, owner unload and no-mod/disabled-mod regression. Native
resource handles require lifetime/generation checks; serialized content needs
stable identities and migration rules. A green generator comparison alone does
not validate cutscenes or inventory behavior.

Validation (Windows, 2026-10-19): Clang 23.1.2 game/test builds passed;
62 affected tests passed, with optional seed-export and symlink privilege skips.
The separate 3,840-case empty/disabled-mod seed comparison still matched pinned
upstream exactly. No PS2 diff. Other native-loader platforms remain unverified.

Native ABI 2 services now share the generic dependency boundary with Lua. They
allocate no content IDs or contributor registries and require explicit package
trust and enablement.

Shop stock/price operations use existing retail definitions only. They own native
lists and base-price pairs without introducing custom item/weapon schemas, IDs or
registries. Future contributor content can integrate through a separately agreed
definition lookup and native allocation contract; these APIs do not reserve it.
