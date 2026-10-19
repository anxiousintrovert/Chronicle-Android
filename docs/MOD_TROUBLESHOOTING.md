# Mod installation and troubleshooting

See [installation and controls](MODS.md), [manifest fields](MOD_FORMAT.md), and
[authoring](MOD_AUTHORING.md).

| Symptom / reason | What to check |
|---|---|
| Package not listed | Use `<save>/mods/<folder>/mod.json` or executable-adjacent `mods/<folder>/mod.json`; unpack archives; remove an accidental extra nesting level; dot-prefixed folders are ignored |
| ON but BLOCKED | ON is the preference, not successful activation; read all detail pages for the blocking reason |
| Next run differs from this run | Saved preferences apply on restart; rescan does not reload active scripts/assets |
| Duplicate mod ID | Remove the second copy from either root; disabling one copy does not resolve duplicate identity |
| Same game file | Choose one package, or obtain an author-provided compatible combined asset; ordering cannot merge binary files |
| Declared conflict / exclusive feature | Disable one incompatible owner; deleting its conflict declaration does not make the mods compatible |
| Missing / disabled / blocked dependency | Install the exact required ID, resolve its errors, enable it, and restart; version ranges are not implemented |
| Dependency cycle | Authors must remove the cycle; reordering folders has no effect |
| Unknown manifest field / unsupported capability | Match schema 1 exactly; do not rename another framework's manifest and expect compatibility |
| Unknown game file / wrong size | Use a target in your extracted game index and an exactly same-size, structurally valid replacement |
| Script error | Inspect the manager's detail pages for the mod ID and Lua error; fix the source/permission/budget issue and restart |
| Cannot read profile | Quit; preserve a copy of `mods-profile.json`, repair its JSON, then restart; an invalid profile is not overwritten automatically |
| Could not save profile | Check save-directory write access/free space; the rejected selection was not applied |
| Save load failure | Read stderr; restore the exact enabled gameplay mod IDs and versions used by the save; malformed/foreign save extensions are rejected |

Use Left/Right in the details pane to see long reasons, quest information, and
the package's resolved directory. The manager uses Chronicle's existing menu UI.

## Diagnostic files

`<save>/mods-report.json` describes the startup scan, package paths, problems, and
resolved load order. It is generated output, not a configuration file. The live
manager also shows subsequent preview scans and script failures; the startup
report is not a live script log. Capture stderr when reporting a load failure:

```powershell
.\darkcloud.exe --data "C:\Games\Chronicle\data" --save "C:\Games\Chronicle\mod-test-save" 2> mod-errors.txt
```

Include the host revision, platform, package versions, relevant manifest, and
exact diagnostic. Logs/reports can contain local paths; review them before sharing.

## Profile format

The manager writes this atomically to `<save>/mods-profile.json`:

```json
{
  "schema_version": 1,
  "enabled": {
    "example.norune-welcome": true,
    "another.mod": false
  }
}
```

Prefer the manager. If repairing it manually, close the game first, keep the file
under 256 KiB, and use Boolean values. IDs absent from the profile use the manifest
default. Profiles are installation preferences, separate from per-slot progress.

## Saves

Keep complete save files together with the gameplay package versions they need.
Do not trim or hand-edit the mod trailer: it includes a checksum over native and
mod data. There is no supported operation to strip requirements or migrate to an
older host. Restoring a pre-mod save is the supported way to resume an unmodded
playthrough. Back up before experimenting with package updates.
