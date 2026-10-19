# Chronicle native plugin ABI 1

This is our C ABI, not MoonMod binary compatibility. Include
[`native_plugin.h`](../port/src/mods/native_plugin.h). A plugin exports
`ChronicleMod_GetPluginV1` returning a static `ChronicleModPluginV1` descriptor
with `size`, `abi = 1`, matching package ID, and start/tick/stop callbacks.
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
At most 128 host calls and 64 queued writes are accepted per native tick. A failed
tick discards queued writes, stops/unloads the plugin, and blocks future ticks.
These budgets limit host service use, not native CPU time or arbitrary native
side effects. A plugin crash can crash the game. Returning success from a write
call means queued; final native validation occurs after successful tick return.

Start returns 1 for success and may set its own `state` pointer. Stop runs for
partial start failure as well as ordinary unload, so it must tolerate partial
initialization. Do not throw across this C ABI. Optional Lua and native code in
one package share permission declarations, but have separate callback transactions.
Native plugin-private memory is transient; SDK 1 does not expose save storage,
Lua callback exports, native event subscriptions or arbitrary engine pointers.

Host API permission checks govern host services only; trust grants native process
execution. The no-mod invariant applies while plugins remain disabled/untrusted.

Host JSON arguments share the structured depth/node/byte limits documented in
[MOD_HOST_API.md](MOD_HOST_API.md). Existing operation field/type contracts remain
unchanged; richer transport alone does not register custom content or cutscenes.
