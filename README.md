<div align="center">

<img src="https://i.imgur.com/pCMkgRM.png" alt="Chronicle" width="700">

**A decompilation project and modern port of Dark Cloud for the PlayStation 2.**

[![Documentation](https://img.shields.io/badge/Documentation-2563EB?style=for-the-badge&logo=readthedocs&logoColor=white)](https://themoonpeople.github.io/Chronicle/)
[![Discord](https://img.shields.io/badge/Discord-5865F2?style=for-the-badge&logo=discord&logoColor=white)](https://discord.gg/RGuE998sDY)

[![NTSC Progress](https://decomp.dev/TheMoonPeople/Chronicle/ntsc.svg?mode=shield&label=NTSC&measure=matched_code_percent)](https://decomp.dev/TheMoonPeople/Chronicle/ntsc)
[![PAL Progress](https://decomp.dev/TheMoonPeople/Chronicle/pal.svg?mode=shield&label=PAL&measure=matched_code_percent)](https://decomp.dev/TheMoonPeople/Chronicle/pal)

[<img src="https://decomp.dev/TheMoonPeople/Chronicle/ntsc.svg?w=512&h=256" width="512" alt="Decompilation progress">](https://decomp.dev/TheMoonPeople/Chronicle/)

</div>

## Chronicle Mod Framework fork

This fork adds a Chronicle-only mod runtime and an in-game manager under
**Options → Mods → Mod Manager**, using the shared menu framework. Install folder
packages for compatible asset replacements, Lua scripts, and custom objectives.
Dependencies, conflicts, enable preferences, and save-bound progress are supported.
Gameplay APIs are experimental; full NG+, new native content registries, and
foreign-format importers remain planned work.

- [Install mods and use the manager](docs/MODS.md)
- [Planned framework features](docs/MOD_ROADMAP.md)
- [Every manifest field and package format](docs/MOD_FORMAT.md)
- [Create a mod: Lua API, lifecycle, testing, distribution, saves](docs/MOD_AUTHORING.md)
- [Troubleshooting and profile format](docs/MOD_TROUBLESHOOTING.md)
- [Working example packages](examples/mods/README.md)
- [Implemented and planned host capabilities](docs/MOD_FRAMEWORK_SCOPE.md)

Use Clang for both C and C++. Framework changes are port-side; `ps2/` is unchanged.
This is a source fork, not a prebuilt release or a distribution of game assets.
It preserves history from [TheMoonPeople/Chronicle](https://github.com/TheMoonPeople/Chronicle)
at `50eaff63` (Player menu framework). GitHub hosts it as a standalone repository
because the owner already has a fork in that network.

## Overview

**Chronicle** is a decompilation project and port of _Dark Cloud_ for the PlayStation 2.

The project aims to produce **100% matching text and data sections** for:

- The main executable of the **NTSC 1.02** release.
- The main executable of the **PAL review** version.
- The `TITLE.BIN` and `DUN.BIN` overlays.

## Building and Running

The PC port supports folder-based asset, Lua, and custom quest mods with an
in-game manager, save-bound mod state, dependencies, and conflict reporting.
See [Mod packages](docs/MODS.md) for installation and the current API, and
[framework scope](docs/MOD_FRAMEWORK_SCOPE.md) for the remaining gameplay services.

### Prerequisites

- A copy of _Dark Cloud_ (ideally the PAL version).

### 1. Clone the repository

```bash
git clone --recurse-submodules https://github.com/anxiousintrovert/Chronicle-Mod-Framework.git
cd Chronicle-Mod-Framework
```

### 2. Provide a disc image

Place your disc image in the corresponding region directory:

| Region    | Path                             |
| :-------- | :------------------------------- |
| PAL       | `rom/pal/Dark Cloud (PAL).iso`   |
| NTSC 1.02 | `rom/ntsc/Dark Cloud (NTSC).iso` |

### 3. Build the project

**Linux / macOS**

```bash
./build.sh <configuration>
```

**Windows**

```batch
build.bat <configuration>
```

### 4. Run the project

```bash
./run.sh <configuration> [arguments]
```

### Build Configurations

| Configuration         | Platform      | Build                               |
| :-------------------- | :------------ | :---------------------------------- |
| `ps2-pal`             | PlayStation 2 | PAL executable and disc image       |
| `ps2-ntsc`            | PlayStation 2 | NTSC 1.02 executable and disc image |
| `linux-x64-release`   | Linux x64     | Release                             |
| `linux-x64`           | Linux x64     | Debug                               |
| `macos-arm64-release` | macOS ARM64   | Release                             |
| `macos-arm64`         | macOS ARM64   | Debug                               |
| `windows-x64-release` | Windows x64   | Release                             |
| `windows-x64`         | Windows x64   | Debug                               |

### Build Options

The following environment variables can be used to customise the build:

| Variable  | Description                                                           |
| :-------- | :-------------------------------------------------------------------- |
| `CLEAN=1` | Removes the selected configuration's build directory before building. |
| `JOBS=N`  | Sets the number of parallel build jobs.                               |

For example:

```bash
CLEAN=1 JOBS=8 ./build.sh linux-x64-release
```
