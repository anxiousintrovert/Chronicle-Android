# Native author probe

This example only logs start/stop; it does not alter gameplay. From the repository
root, using the same Clang/Ninja environment as the game:

```text
cmake -S examples/native-probe -B build-native-probe -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build-native-probe
python tools/chronicle_mod.py check build-native-probe/package
python tools/chronicle_mod.py pack build-native-probe/package --output native-probe.zip
```

Copy the generated `package` directory to your separate test save's
`mods/example.native-probe`. It stays disabled. In the shared-framework mod
manager, review native trust with Triangle/Trust, confirm, enable and restart.
Use [the native SDK](../../docs/MOD_NATIVE_SDK.md) for lifecycle and permission rules.
