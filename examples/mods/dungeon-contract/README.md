# Dungeon Contract example

Copy this directory into your test save's `mods` directory. It is disabled until
you enable it in Options > Mods > Mod Manager and restart. It contains no game
assets and does not create custom items, weapons or cutscenes.

1. Enter gameplay. The quest appears in the package's L1 Quests page as Not started.
2. Open R1 Actions and run Start contract. Return to gameplay to execute it.
3. Enter a dungeon floor after starting (leave and re-enter if already on one).
4. Defeat two enemies through normal gameplay. Scripted kills do not count.
5. Both objectives must finish before the single 50 Gilda reward commits.

The order of objectives does not matter. Opening the quest page never advances
progress. Repeating Start after completion is harmless and grants nothing.
Loading on a floor does not count as entering it. Progress, the start flag and
reward receipt persist with the native save; reloading does not grant the reward
again. An older save restores that older save's progress and money together.
Keep this package enabled at the recorded version when loading its gameplay saves.

`main.lua` registers immutable definitions idempotently on gameplay entry and
from the action. No Lua-local flag stands in for saved quest state. The engine
handles event matching and reward receipts, so there is no reward-per-tick loop.
Changing an existing definition is not a migration: use a new quest ID or an
explicit supported save migration. Definitions are not native story quests or
an importer for another framework's quest format.

Validate from the repository root:

```text
python tools/chronicle_mod.py check examples/mods/dungeon-contract
```

The native test `ModDynamicQuestRuntime.ShippedContractStartsFromMenuAndRestoresWithoutRewardReplay`
loads these exact package files and exercises deferred start, filtered events,
partial save restoration, native Gilda commit and completion receipt restoration.
It is a runtime integration test; it does not certify a full retail playthrough.
