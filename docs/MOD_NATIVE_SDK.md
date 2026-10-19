# Chronicle native plugin SDK

This is our C ABI, not MoonMod binary compatibility. Include
[`native_plugin.h`](../port/src/mods/native_plugin.h). ABI 1 remains supported
without layout changes. It exports `ChronicleMod_GetPluginV1`, returning a static
`ChronicleModPluginV1` descriptor with `size`, `abi = 1`, matching package ID,
and start/tick/stop callbacks. ABI 2 adds lifecycle events and owned save values
through a separate entry point and structs, described below.
The host uses the same Clang toolchain as Chronicle; the fixture verifies the
header as C99 and loads the resulting real Windows DLL.

Native code runs in the game process with the user's OS access. It is not a Lua
sandbox and can alter the engine beyond declared host permissions. Keep ordinary
gameplay packages in Lua where possible. No native library loads from scanning,
copying, trusting alone, or enabling alone. It requires a valid conflict-free,
enabled package and separately saved trust in the local manager, followed by restart.

```json
{
  "schema_version": 1,
  "api_version": 1,
  "id": "example.native-probe",
  "name": "Native Probe",
  "version": "1.0.0",
  "game": "chronicle",
  "requires_capabilities": ["plugins.native.v1", "host.calls.v1"],
  "permissions": ["native.extension", "game.read"],
  "native_library": "probe.dll",
  "files": {}
}
```

Use `.so`/`.dylib` and a platform-appropriate build on other systems. This first
implementation has Windows execution evidence; Linux/macOS/Android loader
behavior remains unverified. There is no portable multi-platform library selector.
The [native-probe source example](../examples/native-probe) builds an independent,
disabled-by-default package and is never installed by the main build.

Trust is bound to SHA-256 of the library bytes plus package ID/version, canonical
package/library paths and declared permissions. Updates require fresh approval.
The digest is checked again before loading. It is not a publisher signature,
hash of transitive OS/library dependencies, or defense against a hostile process
replacing files concurrently. Do not modify packages while running. Revocation
applies after restart, like other manager changes. The Windows loader restricts
dependency lookup to the DLL directory and normal safe DLL search directories.

## Host contract

All callbacks and host calls run on the game thread. Do not retain/use host calls
from worker threads or after stop. The size-tagged host table contains:

- `available(context, operation)`: whether the operation is implemented.
- `call(context, operation, arguments_json, &result_json)`: same operation names,
  validation, permission checks and queued writes as Lua host calls. Arguments
  are one JSON object, at most 16384 bytes. Returns 1 on success, 0 with a JSON
  error. Returned strings belong to the host and last until the next host call.
- `log(context, message)`: bounded session diagnostics visible in the manager.

Game operations are rejected during start/stop. Tick runs only in active gameplay.
At most 128 result-producing host calls and 64 queued writes are accepted per
native callback. A failed callback discards queued writes, rolls back its owned
save changes, stops/unloads the plugin, and blocks the package's later callbacks.
These budgets limit host service use, not native CPU time or arbitrary native
side effects. A plugin crash can crash the game. Returning success from a write
call means queued; final native validation occurs after successful callback return.
Earlier successful callback transactions remain committed. Diagnostics and native
heap/process side effects cannot be rolled back by the framework.

Start returns 1 for success and may set its own `state` pointer. Stop runs for
partial start failure as well as ordinary unload, so it must tolerate partial
initialization. Do not throw across this C ABI. Optional Lua and native code in
one package share permission declarations, but have separate callback transactions.
Native plugin-private memory is transient. ABI 1 does not expose save storage or
event subscriptions. Neither ABI exposes Lua callback exports or arbitrary engine
pointers. ABI 2 supports the optional menu extension described below.

Host API permission checks govern host services only; trust grants native process
execution. The no-mod invariant applies while plugins remain disabled/untrusted.

Host JSON arguments share the structured depth/node/byte limits documented in
[MOD_HOST_API.md](MOD_HOST_API.md). Existing operation field/type contracts remain
unchanged; richer transport alone does not register custom content or cutscenes.

## ABI 2: lifecycle and save values

Declare `plugins.native.v2` in `requires_capabilities`. Export
`ChronicleMod_GetPluginV2`, returning a static `ChronicleModPluginV2` with
`size = sizeof(ChronicleModPluginV2)`, `abi = 2`, matching package ID and non-null
start/tick/stop callbacks. These receive `ChronicleModHostV2`. The loader prefers
V2 when both entry points exist; invalid V2 descriptors fail explicitly rather
than silently falling back to V1. Unknown event bits are rejected.

`event_mask` is the bitwise OR of requested `CHRONICLE_EVENT_*` values; zero
disables notifications. A nonzero mask requires an `event` callback:

```c
int event(const ChronicleModHostV2 *host, void *state,
          uint32_t which, const char *world_json);
```

`which` is one requested bit. `world_json` is a callback-lifetime JSON object
containing `mode`, `map`, `money`, `playing`, `dungeon` and `floor`. It is the
tick's supplied snapshot, not refreshed after each write. Return 1 for success,
0 to roll back this callback's managed changes and stop the package.

| Event bit | Observed trigger |
|---|---|
| `CHRONICLE_EVENT_SAVE_LOADED` | First gameplay tick after successful state load/reset; restored values are already available |
| `CHRONICLE_EVENT_GAME_ENTER` | First gameplay tick or return after a non-gameplay mode |
| `CHRONICLE_EVENT_GAME_LEAVE` | First non-gameplay tick after gameplay; owned values are available but game operations are rejected |
| `CHRONICLE_EVENT_MAP_ENTER` | First observed gameplay map or changed map |
| `CHRONICLE_EVENT_FLOOR_ENTER` | First observed dungeon floor or changed dungeon/floor |
| `CHRONICLE_EVENT_ENEMY_KILLED` | Deferred native/scripted enemy death; requires `events.enemy_killed.v1` for availability |

These use the [Lua lifecycle semantics](MOD_HOST_API.md#owned-lifecycle-listeners).
Floor entry is not a scene-ready/generation-completed hook; game leave is not a
guaranteed shutdown callback. Per package, dispatch order is save-loaded, game
enter/leave, map entry, floor entry, enemy deaths, queued menu actions, Lua tick, native tick.
For each lifecycle event the Lua callback/listeners run before the native callback.
Lua and native callbacks have separate transactions; packages keep dependency order.
No subscribed event callback executes for a zero mask.

Enemy-kill callbacks add an `event` object to `world_json`, using the fields and
delivery limits in [enemy-kill events](MOD_HOST_API.md#enemy-kill-events). This is
an additive ABI 2 event bit; host/plugin struct layouts are unchanged. An older
binary's compiled `CHRONICLE_EVENT_ALL` mask does not request the new bit.

The V2 host adds `state_get(context, key, &result_json)` and
`state_set(context, key, value_json, &result_json)`. They access the same package's
scalar `values` namespace as Lua `chronicle.get/set`. Both return 1 on success,
0 with a JSON error. Missing values return JSON `null`; setting JSON `null`
deletes a key. A successful set returns JSON `true`. A null C pointer is invalid
input, not a deletion. Result strings last until the next result-producing host
call on this package; copy needed strings before another call.

- Access is limited to native tick/event/menu callbacks, never start/stop or workers.
- Keys contain 1–64 lowercase ASCII letters, digits, underscores or periods.
- At most 128 keys per package; replacing/deleting an existing key works at capacity.
- Values are JSON null, boolean, finite number or UTF-8 string of at most 256 bytes
  without NUL. Arrays/objects are rejected; input JSON is limited to 16384 bytes.
- Get/set and game `call` share the 128-call budget per native callback. Discovery
  and logging are separate; this budget is not a native execution sandbox.
- Owned values require no game-read/write permission. Host game operations still
  require their declared permissions; the plugin always needs `native.extension`.

Values use the existing namespaced mod save trailer and exact package/version
checks. Loading/resetting rebinds callbacks to restored/cleared storage; it does
not serialize native pointers or heap state. Failed callbacks or rejected commits
restore only that callback's owned values and discard its queued writes.
Save migration and native cross-package exports remain separate future work.

The [standalone lifecycle example](../examples/native-lifecycle) counts entries
using owned save values without gameplay permissions. It builds with Clang, is
disabled by default, and is never installed by the main game build. No content
registry, custom ID range or cutscene API is introduced; see the
[contributor boundaries](MOD_CONTRIBUTOR_BOUNDARIES.md).

## Native menu actions

ABI 2 plugins may export `ChronicleMod_GetMenuV1` returning a static
`ChronicleModMenuV1` descriptor. This separate optional extension leaves both
published plugin/host layouts unchanged. Declare `menu.native.v1` in
`requires_capabilities`; otherwise an older host can ignore the optional symbol.
No Lua script is needed. Trust, enable and restart requirements are unchanged.

The descriptor specifies its size, `abi = 1`, exact package ID, action count and
array. Each `ChronicleModMenuActionV1` has `size = sizeof(ChronicleModMenuActionV1)`,
ID, title, description and a non-null `run(host, state, world_json)` callback.
The array element size must match exactly; a future element layout requires a
new extension version. Descriptors and their strings must remain immutable with
static lifetime while loaded. A zero count permits a null array.

```c
static int reset(const ChronicleModHostV2 *host, void *state,
                 const char *world_json) {
    const char *result = 0;
    (void)state;
    (void)world_json;
    return host->state_set(host->context, "visits", "0", &result);
}
static const ChronicleModMenuActionV1 actions[] = {
    {sizeof(ChronicleModMenuActionV1), "reset", "Reset visits",
     "Reset the owned visit counter.", reset}
};
static const ChronicleModMenuV1 menu = {
    sizeof(ChronicleModMenuV1), 1, "example.native-lifecycle", 1, actions
};
CHRONICLE_PLUGIN_EXPORT const ChronicleModMenuV1 *ChronicleMod_GetMenuV1(void) {
    return &menu;
}
```

Actions share the package's namespace and 32-action limit with Lua actions.
IDs use the owned-state key syntax; title/description require 1–256 valid UTF-8
bytes without C0/DEL control characters. Duplicate IDs, invalid metadata or an
incompatible descriptor stop the package with a manager diagnostic. Labels are
copied by the runtime and rendered literally, including braces.

The shared manager's R1 Actions pane queues invocation. The callback runs once
at the normal runtime boundary, after entry events and before Lua/native ticks;
multiple queued actions within one package keep request order. It receives the
ABI 2 host, the state pointer from plugin start, and a callback-lifetime world
JSON snapshot. Return 1 to commit or 0 to roll back owned values/queued game writes
and stop the package. Host permissions, 128 host-call/64-write budgets and game
availability checks apply exactly as for native ticks. Native CPU time and direct
process side effects remain outside framework rollback.

Actions cancel on changed gameplay context, failed dependencies, save load/reset
or unload. A failed dependency also unloads its dependent plugin. UI input never
calls the native action directly. Restart clears old descriptors, and unavailable
actions cannot be queued. Runtime results and diagnostics appear in the existing
manager; no parallel menu/input implementation is introduced. The lifecycle example
above includes this extension and requests no gameplay-write permissions.
