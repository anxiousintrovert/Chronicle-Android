# Native counter service example

Standalone ABI 2 example, never installed or enabled by normal builds. It changes
only its owned counter. Build from the repository root with the documented Clang
environment:

```sh
cmake -S examples/native-services -B build-native-services -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build-native-services
python tools/chronicle_mod.py check build-native-services/package
```

Copy the generated package into an isolated save profile's mods folder. Approve
its DLL fingerprint and enable it through the shared manager, then restart.
Rebuilt DLLs require renewed trust. The manifest requests native.extension only.

Consumers declare example.native-services in requires and request game.read for
services.query or game.write for services.command:

```lua
local count = chronicle.call('services.query', {
    provider='example.native-services', name='counter', arguments={}
})
local changed = chronicle.call('services.command', {
    provider='example.native-services', name='increment', arguments={}
})
```

Both exports require empty arguments. Increment returns accepted and count,
limited to 1,000,000. Results are provisional until the outer callback commits;
managed changes roll back on failure. The shared-menu Reset action sets count to
zero. Native consumers use the same operation/JSON through host->call.

See [the SDK](../../docs/MOD_NATIVE_SDK.md#native-service-exports) for descriptors,
result lifetime and limits. Windows Clang build/package and PAL runtime checks
passed; other-platform loading remains unverified.
