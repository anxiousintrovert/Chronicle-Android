# Native lifecycle and owned-state example

This standalone C99 ABI 2 example counts gameplay entries in its own `visits`
save key and logs gameplay/save-loaded events. It requests no game-read or
game-write permission and changes no retail game fields. The normal game build
does not install or enable it.

From the repository root, with Chronicle's Clang/Ninja environment:

```text
cmake -S examples/native-lifecycle -B build-native-lifecycle -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build-native-lifecycle
python tools/chronicle_mod.py check build-native-lifecycle/package
python tools/chronicle_mod.py pack build-native-lifecycle/package --output native-lifecycle.zip
```

Install into an isolated test save. Review Trust in the shared Mod Manager, enable
the package, then restart. Its log should report `Gameplay entered: visits=1`.
Saving attaches the owned value to the mod save trailer. Loading reports the
restored count before the next gameplay-enter event increments it. This is an
event count, not a complete play-session counter: observed gameplay leave/reentry
also increments it. Start/stop never access owned state.

In the shared Mod Manager, open R1 Actions and run **Reset visit counter**. The
native callback resets only this package's owned value at the next safe runtime
callback boundary; the manager displays Completed and a reset log. No Lua wrapper
or new UI is needed. This package requires `menu.native.v1` so older hosts fail
preflight rather than silently omitting the action. Changed library bytes require
fresh local trust.

The count belongs to this package's scalar save namespace. Unload destroys plugin
heap state; only explicitly stored values persist. See [the SDK](../../docs/MOD_NATIVE_SDK.md)
for event ordering, callback transactions, trust and platform limits.
