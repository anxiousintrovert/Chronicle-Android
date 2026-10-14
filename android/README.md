# Chronicle for Android

This project packages Chronicle's native game port as an Android app for `arm64-v8a` and `x86_64`. The
app is named Chronicle and uses SDL3 with the Kazarov Vulkan renderer. The renderer requires Vulkan 1.3
and the device features documented in [`../tools/kazarov/port/gfx/README.md`](../tools/kazarov/port/gfx/README.md).

The app includes a multi-touch PS2-style controller overlay. It hides when a hardware gamepad, keyboard, or
mouse is connected, and handles Foldable display changes. Android settings, renderer integration, and
performance work are still in progress.

## Game data

The APK contains no Dark Cloud disc image or extracted game data. On first launch, use the system file picker
to choose your own PAL disc image. Chronicle imports the required files into private app storage. Do not
commit or distribute disc images, `DATA.DAT`, `DATA.HD2`, saves, or extracted game assets with this source.

## Prerequisites

- JDK 17.
- Android SDK platform 35 and build tools 35.0.0.
- Android NDK 27.0.12077973.
- CMake 3.31.6.
- Windows PowerShell for the dependency setup script below.

Gradle 8.13 is pinned by the checked-in wrapper configuration. `fetch-deps.ps1` downloads SDL 3.4.16 and
the Windows glslang 16.6.0 shader compiler. It verifies the downloaded archive hashes before extracting
the dependencies into ignored local build folders.

## Build a debug APK

Clone the repository and initialize its renderer submodule:

```powershell
git clone --recurse-submodules https://github.com/anxiousintrovert/Chronicle-Android.git
cd Chronicle-Android\android
```

Then fetch the build dependencies and assemble the APK:

```powershell
.\fetch-deps.ps1
.\gradlew.bat assembleDebug
```

The output is `app\build\outputs\apk\debug\app-debug.apk`. The APK contains both `arm64-v8a` and
`x86_64` native libraries. No game image is needed to compile; the player selects their own PAL disc when
setting up the app.

## Maintainer test packages

`publish-test-apk.ps1` builds the debug APK, makes a timestamped ZIP for easier Nextcloud downloads, and
moves previous Chronicle/Dark Cloud test APKs and ZIPs into `DARKCLOUDBUILDS`. It leaves the current ZIP at
the destination root. The script defaults to the maintainer's mapped `Z:\` Games share; provide
`-DestinationRoot` to use a different writable folder:

```powershell
.\publish-test-apk.ps1 -DestinationRoot 'D:\Chronicle-builds'
```
