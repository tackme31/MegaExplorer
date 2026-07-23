# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project status

This is an early-stage skeleton. `main.cpp` and `Main.qml` are still the stock Qt Creator "Hello World"
boilerplate — no MEGA integration exists yet. Treat the README's feature list and roadmap (below) as the
target design, not the current state, and check current file contents before assuming any feature exists.

## What this project is

A Windows-Explorer-like desktop client for MEGA cloud storage (Japanese README, translated summary below),
built to cover gaps in the official MEGAsync app: thumbnail grid view, search, and double-click-to-open.
It is explicitly **not** a full bidirectional sync client — the only background activity planned is a
single background refresh when a folder is opened, not continuous watching.

Planned stack (per `README.md`), not yet wired into the build:
- **MEGA access**: MEGA C++ SDK (`meganz/sdk`, BSD-2-Clause) — requires an app key from mega.io/developers
- **UI**: Qt 6 Quick/QML, Qt Quick Controls 2
- **Build**: CMake + vcpkg (vcpkg not yet present in this repo)
- **Thumbnails/previews**: FreeImage or Qt's own decoding; FFmpeg for video
- **Local cache**: SQLite for the node tree and thumbnail cache

Licensing note: the app itself is GPLv3 (via Qt); the MEGA SDK is BSD-2-Clause and MEGAsync's own source
(`meganz/MEGAsync`) is under a restrictive Code Review Licence — do not copy code from MEGAsync, only use
it as a reference for SDK usage patterns.

Development roadmap is bottom-up (see `README.md` for full detail): Phase 0 (SDK build + CLI login/fetchNodes)
→ Phase 1 (bare file listing) → Phase 2 (thumbnails) → Phase 3 (search) → Phase 4 (download/open/upload) →
Phase 5 (local cache, background refresh on folder open) = MVP. Realtime remote-change reflection is a
post-MVP Phase 6; full bidirectional local mirroring is explicitly out of scope for the foreseeable future.

## Build

Requires Qt 6.10+ (repo was configured against Qt 6.11.1, MinGW 64-bit toolchain at
`C:/Qt/6.11.1/mingw_64`) and CMake 3.16+. There is no `CMakePresets.json`; the existing build directory
was configured directly by Qt Creator using Ninja.

To reconfigure/build from the command line:

```
cmake -S . -B build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64
cmake --build build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug
```

The resulting binary is `build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug/appMegaExplorer.exe`.

No test target, linter config, or CI is currently set up in this repo.

## Architecture

- `main.cpp` — minimal `QGuiApplication` + `QQmlApplicationEngine` bootstrap; loads the `MegaExplorer`
  QML module's `Main` component via `loadFromModule`.
- `Main.qml` — root `ApplicationWindow`. QML sources are registered as a Qt QML module (URI `MegaExplorer`)
  in `CMakeLists.txt` via `qt_add_qml_module`; new `.qml` files must be added to that `QML_FILES` list.
- `importedcontent/` — reserved drop-in location for designs exported from Figma to Qt. If
  `importedcontent/CMakeLists.txt` exists, the root `CMakeLists.txt` automatically `add_subdirectory`s it;
  currently empty (only a placeholder README).
- `build/` — out-of-source Qt Creator build directory (Ninja + MinGW); not source, safe to ignore/regenerate.
- `.clangd` — points clangd at the compilation database in
  `build/Desktop_Qt_6_11_1_MinGW_64bit_Debug` (note: this differs from the actual build dir name
  `Desktop_Qt_6_11_1_MinGW_64_bit_Debug` — an underscore is missing before `bit`).

When the MEGA SDK integration begins, expect a new C++ layer between the QML UI and the SDK's C++ API
(the SDK is not QML-friendly out of the box), plus new CMake `find_package`/`target_link_libraries` wiring
and likely a vcpkg manifest that doesn't exist yet.
