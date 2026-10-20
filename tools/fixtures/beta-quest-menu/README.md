# PAL Windows quest menu/save controller fixtures

These recorded pad sequences exercise the shipped Dungeon Contract 1.1.0 using
the staged Clang-built game, not direct calls to quest or save functions. They
use the existing `--input` facility and shared Options/mod menus. They were run
with PAL data and the default controls. No game data or save is included here.

Use a **new, disposable save directory**, with only Dungeon Contract installed.
Do not run the new-game/save sequence against personal saves: it writes slot 1.
From the repository root, copy `examples/mods/dungeon-contract` into that save
directory's `mods` directory, or use the documented pack/install commands.
Keep the package disabled initially; the first fixture enables it through the UI.

Set `$game`, `$data`, `$save`, and `$evidence` to absolute local paths. `$evidence`
must be a separate existing directory. Run each fixture in a new process:

```powershell
$fixture = (Resolve-Path tools/fixtures/beta-quest-menu).Path
& $game --offscreen --jump title --fast-load --frames 650 --data $data --save $save --input "$fixture/enable.input" --screenshot "$evidence/enable.png"
& $game --offscreen --jump title --fast-load --frames 28000 --data $data --save $save --input "$fixture/start-save.input" --screenshot "$evidence/save.png"
& $game --offscreen --jump title --fast-load --frames 2190 --data $data --save $save --input "$fixture/load.input" --screenshot "$evidence/load.png"
& $game --offscreen --jump title --fast-load --frames 2500 --data $data --save $save --input "$fixture/abandon.input" --screenshot "$evidence/abandon.png"
& $game --offscreen --jump title --fast-load --frames 2850 --data $data --save $save --input "$fixture/restart.input" --screenshot "$evidence/restart.png"
```

Check each process exit code and review the screenshots. Expected results:

| Fixture | Expected evidence |
|---|---|
| enable | Next run enabled, current run inactive; profile written by the manager |
| start-save | Normal new-game opening, Start contract through town Options/Actions, then native Save/Load > Save; final screen says Saving completed |
| load | Title Load restores the new slot; shared quest page shows In progress, 0/3 |
| abandon | Loads that slot, runs Abandon contract; quest page shows Not started, 0/3 |
| restart | Loads that slot, abandons, then starts again; page shows In progress, 0/3 |

The new-game sequence accepts an all-A test name. It reaches Norune but does not
complete the mayor introduction, so dungeon travel is still story-locked. Its
attempt to travel before saving is refused without changing that flag. The
load sequence assumes the title defaults to Load when slot 1 exists; this differs
from the empty-save title selection used by the enable fixture. Run enable only
once: replaying it toggles the package off.

Inspect `$save/saves/1/save.dat` with `chronicle_mod.py inspect-save`: the trailer
must have a valid checksum and require `example.dungeon-contract` version 1.1.0.
The local acceptance run also inspected owned state and found
`quest_started.contract = true`. Loading and browsing do not change the saved
file. Abandon/restart here modify the live state only; neither fixture resaves it.

This proves controller-driven start, save/load, cancellation and restart for an
unadvanced contract. It does **not** prove floor objective progression, ordinary
combat kills, completed rewards or completed-state reload. Those remain separate
beta acceptance work. No story flags, actor HP or event queues are edited.
