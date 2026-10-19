# Creating, testing, and distributing a mod

## First working package

1. Copy [norune-welcome](../examples/mods/norune-welcome) to an independent test
   save directory as `mods/norune-welcome/`. `mod.json` must be immediately inside
   that folder, not inside another nested copy of the package.
2. Start the port with your extracted data and isolated save directory:

   ```powershell
   .\darkcloud.exe --data "C:\Games\Chronicle\data" --save "C:\Games\Chronicle\mod-test-save"
   ```

   On Linux/macOS, use `./darkcloud` and your own paths. This command requires an
   already built executable and extracted game data; it is not an ISO importer.
3. Open **Options → Mods → Mod Manager**. Enable the package, confirm there are
   no reported conflicts, then restart. Enter Norune: the quest reaches 1/1 and awards 50 Gilda.
4. Save, quit, and reload using the same package version. The quest and reward
   guard should persist; the reward should not be granted a second time.
5. To make your own mod, change `id`, `name`, and quest text, then restart. Keep
   your released ID stable. Use [the manifest reference](MOD_FORMAT.md) for every
   field. Never copy the example ID into a second installed package.

## Lua entry point and lifecycle

The host runs `main.lua` at runtime initialization; it must return a table.
Both callbacks are optional. Each mod owns a separate Lua environment.

For package creation, validation, conflict previews and ZIP packaging, see
[author tools and native host calls](MOD_HOST_API.md). For compiled extensions,
see [the native SDK](MOD_NATIVE_SDK.md). Examples are separate packages, never
installed or enabled by the framework build.

```lua
return {
    on_map_enter = function(world)
        -- First gameplay observation after load, or a map change.
    end,
    on_tick = function(world)
        -- One gameplay simulation step, not a render frame.
    end
}
```

`world` is a snapshot table with integer `mode`, `map`, `money`, and boolean
`playing`. Changing it does not change the game. Native mode numbers are not a
high-level event API. Callbacks run only during town/dungeon gameplay. A map-entry
step applies built-in visit progress, calls `on_map_enter`, then calls `on_tick`.
Dependency modules run before dependents. The money snapshot may precede rewards
queued earlier in the same simulation step: do not use it as an account ledger.

Loading a save recreates Lua environments and reruns entry points with restored
state. New-game initialization recreates them with fresh state. Lua locals and
globals are transient. Do not overwrite persistent values unconditionally at
top level; initialize a key only when `chronicle.get(key)` is nil. Top-level
quest or currency updates are rejected because gameplay is not active there.

### API return values and errors

| Operation | Successful result |
|---|---|
| `chronicle.supports(name)` | Boolean |
| `chronicle.get(key)` | Scalar value or nil for absent key |
| `chronicle.set(key, value)` | `true`; nil value deletes the key |
| `chronicle.quest_get(id)` | Integer progress, initially zero |
| `chronicle.quest_add(id, amount)` | New integer progress, capped at goal |
| `chronicle.add_money(delta)` | `true` when the request is accepted for this callback |

Validation failures generally return `nil, reason`; wrong Lua argument types
can raise an error. `assert(chronicle.set(...))` turns a rejected operation into
a callback failure with its reason. A failing callback rolls back its scalar
state writes, manual quest changes, and queued money. Built-in map visits happen
before that callback and are not part of its rollback. The failed script stops;
dependent scripts stop too. Independent scripts continue. Check diagnostics in
the manager. `pcall`/`xpcall` are unavailable.

### One-time rewards

```lua
return {
    on_tick = function(world)
        if chronicle.quest_get("welcome") >= 1
            and not chronicle.get("reward_paid") then
            assert(chronicle.add_money(50))
            assert(chronicle.set("reward_paid", true))
        end
    end
}
```

Declare `economy.write` permission. Positive and negative money requests, and
their net callback total, must stay within −32,767…32,767. The native balance is
clamped to 0…65,535. Acceptance is not proof that the full amount fits: this example
still marks the reward paid when the player is at the cap. Choose and document
your own cap policy. A negative delta is not a conditional purchase operation.

For a script-controlled quest, omit its manifest `map` and call
`assert(chronicle.quest_add("objective", 1))` when your condition becomes true.
Persist a guard if the condition can be true on multiple ticks. Avoid incrementing
on every `on_map_enter` without a guard: that callback also occurs after loading.

### Persistence and runtime limits

State is private to each package ID. Keys are 1–64 bytes using lowercase ASCII
letters, digits, `_` and `.`; 128 keys per mod. Values are booleans, finite numbers,
or strings up to 256 bytes. Use text without embedded NUL bytes; the host API uses
C strings. Tables, functions, objects, native pointers, and arbitrary binary blobs
cannot be saved. Do not depend on exact large-integer round trips through numeric
state. Quest progress is maintained separately from scalar keys.

Available libraries: base functions (with removals), `math`, `string`, `table`,
and `utf8`. No `require`, filesystem, process, network, debug, coroutine, native
plugin loading, or dynamic Lua loading. `print`, `collectgarbage`, metatable
accessors, and protected calls are removed. Budget: 8 MiB Lua allocation per mod,
100,000 Lua instructions per chunk/callback, 1 MiB source file. These do not bound
all host allocations or time spent inside C library calls. Install trusted mods.

## Test checklist

- Validate JSON syntax; install the actual directory and inspect the manager.
  Syntax-only JSON validation cannot verify game paths or capability support.
- Confirm enable/disable preferences survive restart and live activation changes
  only after restart.
- Exercise fresh game, manual save/load, autosave/load, and a legacy unmodded save.
- Check one-time effects do not repeat after save/load or menu transitions.
- Install a conflicting package and a dependent package; verify readable reasons
  and that disabling the conflict permits activation on restart.
- Verify missing/changed gameplay mod versions reject incompatible saves. Keep
  the original matching packages and saves; automatic migrations are unavailable.
- Test real replacement assets in the relevant scene. Matching byte lengths and
  passing loader tests do not prove format compatibility.

## Distribution and updates

Ship one folder with `mod.json`, Lua/assets it references, a README, license, and
changelog. You may distribute a ZIP containing that folder, but users must unpack
it; the runtime does not read ZIPs. Include installation location, tested host
revision/platform, required capabilities/mods, known conflicts, permissions,
gameplay/save effects, and removal instructions. Distribute only assets you have
permission to share; the examples contain no extracted game data.

Use exact package versions in release notes. A gameplay save requires its recorded
version strings. Updating even `1.0.0` to `1.0.1` currently prevents loading those
saves until matching versions are restored. Keeping the string unchanged does
not make incompatible code safe. No migration or force-load command exists.

Disable through the manager and restart to stop a mod. Removing a gameplay mod
does not undo native effects already saved, and saves requiring that mod will
refuse to load without it. Retain a pre-mod save for returning to an unmodded run.
Asset-only packages have no Lua/quest state requirement, but removal still changes
which native assets are read.

## Extending the host

Host adapters belong under `port/`; never edit `ps2/`. Use Chronicle's shared `UI`
menu framework and existing port overrides. Expose tested, versioned capabilities
before advertising support. Lua menu creation is not exposed in API 1; the shared
UI is used by the native manager. See [scope](MOD_FRAMEWORK_SCOPE.md).

Both C and C++ compilers must be Clang. See the root build instructions for host
dependencies and data extraction. For direct CMake configuration, explicitly pass
`-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++` (or absolute paths from the
same toolchain), alongside the host's required preset/dependency arguments.
Do not reuse a build cache configured for another compiler.

```text
cmake --build <build> --target darkcloud darkcloud_tests
ctest --test-dir <build> -R "ModManager|ModRuntime|DataRead|SaveSlots|MenuOption|Localize|IntegrationGameloop" --output-on-failure
```

The published change was validated on Windows with Clang 23.1.2: 109 tests passed,
one symlink test skipped, plus offscreen in-game manager/quest/conflict checks.
Linux, macOS and Android behavior have not been validated for this framework.
