# Windows mod framework beta testing

This is the tester procedure for the forthcoming Windows beta. A published beta
candidate must identify its source commit and checksums. This guide does not
declare the current branch release-ready: outstanding acceptance evidence is
tracked in [the release checklist](MOD_FIRST_RELEASE.md#testing-beta-acceptance-gates).

## Prepare an isolated test

Use your own extracted PAL game data and a separate test save directory. Close
Chronicle and back up any save you intend to copy before testing. Keep normal
saves outside the test directory. Gameplay saves can require the packages and
versions used to create them; disabling a quest package is not a save conversion.

For source builds, follow [Windows build instructions](WINDOWS.md), using the
documented llvm-mingw Clang toolchain and `build.bat windows-x64-release`.
Launch from PowerShell, adjusting these absolute paths:

```powershell
& "C:\Chronicle\port\build\windows-x64-release\darkcloud.exe" --data "C:\ChronicleData" --save "C:\ChronicleBetaSave" 2> "C:\ChronicleBeta-stderr.log"
```

For a distributed candidate, replace the executable path with its `darkcloud.exe`.
Keep its supplied runtime DLLs beside it. Install a working Vulkan graphics driver.
Game data, personal saves and enabled gameplay packages are not part of the beta
distribution. Begin with no packages in either the test save's `mods` directory
or the `mods` directory beside the executable. Confirm ordinary launch, gameplay
and save/load before adding packages; record any failure against this baseline.

## Install and manage packages

Use one directory per package, with `mod.json` directly inside it:

```text
C:\ChronicleBetaSave\mods\author.package\mod.json
C:\ChronicleBetaSave\mods\author.package\main.lua
```

Other files depend on the package: Lua is not required for texture-only mods.
See [the manifest reference](MOD_FORMAT.md), [quest authoring](MOD_AUTHORING.md)
and [full-resolution textures](MOD_TEXTURES.md) for supported formats.
Do not place a ZIP directly in `mods`. From the source checkout, the offline tool
can validate and extract an archive without enabling or executing it:

```powershell
python tools/chronicle_mod.py install "C:\Downloads\author.package.zip" --mods "C:\ChronicleBetaSave\mods"
```

Open **Options > Mods > Mod Manager**. New packages must be disabled. Select one
with Up/Down and confirm to toggle, or use the mouse Toggle button. Left/Right
pages through details; Square/Rescan scans package changes. Current-session
activation and next-launch selection are separate. **Exit and restart to apply
enable, disable or package changes.** Rescan does not reload live scripts/assets.

Read the displayed dependency/conflict reason when a package cannot activate.
Do not assume one conflicting replacement wins. Disable the conflicting package,
restart and check the result. Native plugins additionally require the separate
Trust confirmation; enabling alone does not approve native code. Native plugins
are not sandboxed. They are not required for the quest/texture acceptance below.

## Quest acceptance

Copy `examples/mods/dungeon-contract` into the test save's `mods` directory.
Enable Dungeon Contract, restart, and enter gameplay.

1. Open its **L1 Quests** page and confirm the contract is not started.
2. Open **R1 Actions**, select **Start contract**, press Cross/Run, and return to
   gameplay. Actions execute on a gameplay tick, not while browsing the menu.
3. Enter a dungeon floor after starting. Loading a save already on a floor does
   not count as entering it. Defeat two enemies using normal game controls.
4. Check both objectives and exactly one 50 Gilda reward. Record money around the
   final objective, accounting separately for ordinary pickups or other rewards.
5. Save and reload partial progress and completed progress in separate test saves.
   Confirm the corresponding progress and money restore together, and repeating
   Start or reloading completion does not grant another reward.
6. On an unfinished run, use **Abandon contract**, return to gameplay, then Start
   again. Progress should restart at zero. Abandoning a completed contract must
   not reopen its reward. Browsing the quest page must never advance progress.

Keep the package at its recorded version when loading these saves. The shipped
example README documents the supported migration. Controlled native-event and
disk-save tests already exist; report this normal-controls playthrough separately.

## Texture acceptance

Use a package matching the original texture identity in your own data. Follow
[texture discovery and packaging](MOD_TEXTURES.md) to create one. Raw `files`
replacements require the expected original byte size; PNG `textures` replacements
can have different resolutions while retaining the original logical coordinates.

Compare the same scene and settings across fresh launches: no package, enabled
replacement, disabled replacement, and two enabled packages claiming the same
target. Check the replacement appears only when active and original appearance
returns when disabled or blocked. Record package IDs and the conflict reason.
Repeat with a 4096x4096 PNG, checking colors, transparency and atlas alignment.
Higher pixel dimensions alone do not add artistic detail to an upscale.

The automated PAL smoke runner and its evidence limits are documented in the
texture guide. Its magenta diagnostic is intentional; the upscaled-original case
should retain original colors. Static PNG replacement does not reproduce palette
cycling/recoloring. Broader copy paths, device-memory limits and other platforms
remain unverified.

## Report a result

Include candidate commit/checksum, Windows version, GPU/driver, game region,
package IDs/versions, exact steps, expected/actual behavior and whether the same
issue occurs with no packages. Attach before/after screenshots where useful.
Include the test save directory's `mods-report.json` and captured stderr. Preserve
`mods-profile.json` when diagnosing enable selections; a corrupt profile blocks
activation and should be reported before attempting repair.

Do not upload disc images or extracted game assets. Share saves only deliberately
and with the necessary package/version context. Note whether evidence came from
normal controls, an automated fixture or source inspection. Windows results do
not certify Android, Linux or macOS. Planned features are listed separately in
[the roadmap](MOD_ROADMAP.md).

## Maintainer: stage a Windows candidate

Build the checkout with the documented Clang toolchain first. The staging helper
copies an explicit runtime list, source language/glyph assets, mod documentation
and dependency notices into a new directory. It does not copy build-tree saves,
mods, profiles, captured textures or game data. Run from the source checkout:

```powershell
python tools/chronicle_windows_bundle.py --build "C:\Chronicle\port\build\windows-x64-release" --dependencies "C:\Chronicle\.kazarov\windows\deps" --output "C:\Candidates\chronicle-beta"
```

The output parent must exist; the destination must not. Missing required inputs,
non-Clang compiler records and PS2 output paths fail. This helper expects the
documented dependency layout and fetched CMake dependency sources. A build with
externally supplied libraries needs its corresponding notices collected first.

`bundle.json` records per-file SHA-256 hashes, sizes, compiler records and the
checkout identity/dirty flag. Checkout identity alone does not prove which source
produced an existing binary: retain the clean build log and exact source commit.
The manifest itself is not signed and does not certify a release. Acceptance
checks and notice review remain required, including the supplied glyph artwork's
missing attribution noted in `notices/Glyph-provenance.md`. Keep test saves and
logs outside this bundle. To verify and archive a candidate locally:

```powershell
python tools/chronicle_bundle_archive.py --bundle "C:\Candidates\chronicle-beta" --output "C:\Candidates\chronicle-beta.zip"
```

The archive and its `.sha256` sidecar must not already exist. The tool checks
every recorded file's size and hash, rejects unlisted files and linked inputs,
and reads every ZIP entry back before writing the checksum. Sorted entries and
fixed ZIP metadata make the same bundle reproducible with the same Python/zlib
toolchain. This checks integrity against the local manifest, not a signature or
independent proof of source provenance. It does not publish or certify a release;
complete the acceptance gates and notice review before distributing the archive.

### Repeat the staged installation rehearsal

Use an original player-body discovery snapshot and a visibly different replacement
PNG made from your own PAL data. The runner creates two default-disabled texture
packages, packs and installs them through the author CLI, then uses scripted pad
input to navigate the title Options screen and shared mod manager. It never writes
an enable profile itself. All generated assets, profiles and screenshots stay local.

```powershell
python tools/chronicle_beta_install_smoke.py --bundle "C:\Candidates\chronicle-beta" --data "C:\ChronicleData" --snapshot "C:\Captures\body.texture" --image "C:\Captures\diagnostic.png" --output "C:\Audits\new-install-run"
```

The runner verifies bundle hashes first and uses a new external test save directory.
On Windows it removes development-tool directories from the child game's PATH.
It records startup reports separately from next-launch selections, checking actual
activation only across separate processes. Every phase saves the controller input,
log, screenshot and report; changed profiles are copied as evidence, never edited.
The final profile must disable both packages. `result.json` records checked phases.

Review the scene screenshots separately: enabled replacement must be visible;
conflict and disabled scenes should recover the original. Successful report checks
alone do not establish rendering correctness. This uses startup jumps to the title
and a fixed town scene; it does not certify normal quest combat, new-game progression
or save/load. The controller timing is a PAL Windows fixture, not a general UI bot.
