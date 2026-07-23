# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project status

**Phase 0 (MEGA SDK build verification + CLI login/fetchNodes) is complete.** The MEGA C++ SDK
(`meganz/sdk`, pinned `v10.17.0`) and `vcpkg` are vendored as git submodules under `third_party/`,
and `appMegaExplorer` links against `MEGA::SDKlib`. `main.cpp` and `Main.qml` are still the stock Qt
Creator "Hello World" boilerplate, though — no MEGA UI wiring exists yet (Phase 1). Treat the
feature list and roadmap below (full detail in `MEMO.md`, not `README.md` — `README.md` is just a
one-line title stub) as the target design for everything past Phase 0, and check current file
contents before assuming a feature exists.

See "MEGA SDK integration" further down for the toolchain/build details that came out of Phase 0.

## What this project is

A Windows-Explorer-like desktop client for MEGA cloud storage (Japanese README, translated summary below),
built to cover gaps in the official MEGAsync app: thumbnail grid view, search, and double-click-to-open.
It is explicitly **not** a full bidirectional sync client — the only background activity planned is a
single background refresh when a folder is opened, not continuous watching.

Planned stack (full detail in `MEMO.md`):
- **MEGA access**: MEGA C++ SDK (`meganz/sdk`, BSD-2-Clause) — vendored as a submodule, see below.
  No app key is required (MEGA dropped that requirement in 2023; pass `nullptr` to `MegaApi`'s
  constructor — see `meganz/sdk` issue #2706).
- **UI**: Qt 6 Quick/QML, Qt Quick Controls 2
- **Build**: CMake + vcpkg — both vendored/wired as of Phase 0, see "MEGA SDK integration" below
- **Thumbnails/previews**: FreeImage or Qt's own decoding; FFmpeg for video
- **Local cache**: SQLite for the node tree and thumbnail cache

Licensing note: the app itself is GPLv3 (via Qt); the MEGA SDK is BSD-2-Clause and MEGAsync's own source
(`meganz/MEGAsync`) is under a restrictive Code Review Licence — do not copy code from MEGAsync, only use
it as a reference for SDK usage patterns.

Development roadmap is bottom-up (see `MEMO.md` for full detail): **Phase 0 (SDK build + CLI
login/fetchNodes) is done** → Phase 1 (bare file listing, next up) → Phase 2 (thumbnails) →
Phase 3 (search) → Phase 4 (download/open/upload) → Phase 5 (local cache, background refresh on
folder open) = MVP. Realtime remote-change reflection is a post-MVP Phase 6; full bidirectional
local mirroring is explicitly out of scope for the foreseeable future.

## Build

Requires Qt 6.10+ and CMake 3.20+ (bumped from 3.16 in Phase 0 — `meganz/sdk`'s own minimum).
There is no `CMakePresets.json`.

**As of Phase 0, MSVC is the toolchain going forward**, not MinGW — `meganz/sdk`'s Windows build is
only documented/supported for MSVC (VS2022) + vcpkg; MinGW is untested against the SDK's vcpkg
dependency set. Qt 6.11.1 is installed for both `C:/Qt/6.11.1/mingw_64` (legacy, pre-SDK) and
`C:/Qt/6.11.1/msvc2022_64` (current). The old MinGW build directory
(`build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug`) is left untouched but is no longer being built against
— it predates the SDK being linked in and will not have `MEGA::SDKlib`.

This repo vendors the MEGA SDK and vcpkg as git submodules (`third_party/sdk` pinned at `v10.17.0`,
`third_party/vcpkg` at full history — **do not shallow-clone vcpkg**, its manifest/baseline
resolution needs the full commit history or `cmake` configure fails trying to `git show` historical
`versions/baseline.json` blobs). On a fresh clone: `git submodule update --init --recursive`, then
`third_party/vcpkg/bootstrap-vcpkg.bat`.

To reconfigure/build the main project from the command line (MSVC):

```
cmake -S . -B build/Desktop_Qt_6_11_1_MSVC2022_64bit_Debug -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64 -DCMAKE_BUILD_TYPE=Debug ^
    -DVCPKG_ROOT=third_party/vcpkg ^
    -DCMAKE_TOOLCHAIN_FILE=third_party/vcpkg/scripts/buildsystems/vcpkg.cmake ^
    -DVCPKG_MANIFEST_DIR=third_party/sdk ^
    -DVCPKG_TARGET_TRIPLET=x64-windows-mega ^
    -DVCPKG_OVERLAY_PORTS=third_party/sdk/cmake/vcpkg_overlay_ports ^
    -DVCPKG_OVERLAY_TRIPLETS=third_party/sdk/cmake/vcpkg_overlay_triplets ^
    -DVCPKG_MANIFEST_FEATURES="use-openssl;use-freeimage;use-ffmpeg;use-pdfium"
cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit_Debug --config Debug --target appMegaExplorer
```

Why all the `VCPKG_*` flags: `meganz/sdk`'s own CMake only auto-configures vcpkg (triplet, overlay
ports/triplets, manifest features) when SDKlib is the *top-level* CMake project (its
`process_vcpkg_libraries` macro is gated behind `if(NOT PROJECT_NAME)`, and `CMAKE_TOOLCHAIN_FILE`
must be set before the first `project()` call anyway). Since we `add_subdirectory` it under our own
`MegaExplorer` project, that auto-configuration never runs — the parent project has to replicate it
manually, which is what these flags do. The manifest feature list mirrors the SDK's own defaults for
a Windows desktop build (`USE_OPENSSL`/`USE_FREEIMAGE`/`USE_FFMPEG`/`USE_PDFIUM` = ON by default in
`third_party/sdk/cmake/modules/sdklib_options.cmake`; `USE_LIBUV`/`USE_READLINE` = OFF on Windows) —
check that file if the SDK version bumps and defaults change.

**Build only the `appMegaExplorer` target, not the full solution/`ALL` target**: the SDK's own
`gfxworker` tool (its out-of-process thumbnail/GFX worker, relevant from Phase 2 on) currently fails
to link (`LNK2019` unresolved FFmpeg `sws_*` symbols) in this configuration — unrelated to
`appMegaExplorer` itself, not yet root-caused. Needs revisiting before Phase 2 if isolated GFX
processing is used.

The resulting binary is `build/Desktop_Qt_6_11_1_MSVC2022_64bit_Debug/Debug/appMegaExplorer.exe`. It
needs Qt's `bin` dir and vcpkg's installed `debug/bin` dir (for FFmpeg etc. DLLs) on `PATH` to run
outside Qt Creator, e.g.:
```
set PATH=C:\Qt\6.11.1\msvc2022_64\bin;%CD%\build\Desktop_Qt_6_11_1_MSVC2022_64bit_Debug\vcpkg_installed\x64-windows-mega\debug\bin;%PATH%
```

To also validate the SDK in isolation (e.g. after bumping the pinned SDK version), the standalone
build used during Phase 0 still works and is a good first sanity check before touching the main
project:
```
cmake -S third_party/sdk -B build/sdk-msvc-debug -G "Visual Studio 17 2022" -A x64 ^
    -DVCPKG_ROOT=third_party/vcpkg -DCMAKE_BUILD_TYPE=Debug -DENABLE_SDKLIB_TESTS=OFF -DENABLE_SDKLIB_WERROR=OFF
cmake --build build/sdk-msvc-debug --config Debug
```
(`ENABLE_SDKLIB_WERROR=OFF` is needed here specifically because this build's `ENABLE_SDKLIB_WERROR`
defaults to `ON` when SDKlib is standalone/top-level; it defaults `OFF` — and so isn't needed — when
embedded via `add_subdirectory` as the main project does. The warning being promoted to an error is
`C4819`, an MSVC warning about source characters outside the current codepage — harmless, just
noisy, on a non-English-locale Windows machine.) This produces
`build/sdk-msvc-debug/examples/simple_client/Debug/simple_client.exe`, a minimal official SDK sample
that logs in and lists the root folder given `MEGA_EMAIL`/`MEGA_PWD` env vars — useful for isolating
"is it the SDK/vcpkg build, or our own CMake wiring" if something breaks later.

No test target, linter config, or CI is currently set up in this repo.

## Architecture

- `main.cpp` — minimal `QGuiApplication` + `QQmlApplicationEngine` bootstrap; loads the `MegaExplorer`
  QML module's `Main` component via `loadFromModule`.
- `Main.qml` — root `ApplicationWindow`. QML sources are registered as a Qt QML module (URI `MegaExplorer`)
  in `CMakeLists.txt` via `qt_add_qml_module`; new `.qml` files must be added to that `QML_FILES` list.
- `importedcontent/` — reserved drop-in location for designs exported from Figma to Qt. If
  `importedcontent/CMakeLists.txt` exists, the root `CMakeLists.txt` automatically `add_subdirectory`s it;
  currently empty (only a placeholder README).
- `third_party/sdk` — `meganz/sdk` git submodule (pinned `v10.17.0`), `add_subdirectory`'d from the
  root `CMakeLists.txt`; exposes the `MEGA::SDKlib` target that `appMegaExplorer` links against.
  BSD-2-Clause; do not edit files in here directly, it's vendored.
- `third_party/vcpkg` — `microsoft/vcpkg` git submodule (full history, not shallow — see Build
  section), used to build the SDK's third-party dependencies (cryptopp, OpenSSL, FreeImage, FFmpeg,
  pdfium, curl, icu, sqlite3, etc.) per `third_party/sdk/vcpkg.json`'s manifest.
- `build/` — out-of-source build directories; not source, safe to ignore/regenerate. Multiple exist:
  the original MinGW one (pre-SDK, stale), `Desktop_Qt_6_11_1_MSVC2022_64bit_Debug` (the main
  project, current), and `sdk-msvc-debug` (standalone SDK-only validation build from Phase 0).
- `.clangd` — points clangd at the compilation database in
  `build/Desktop_Qt_6_11_1_MinGW_64bit_Debug` (note: this differs from the actual build dir name
  `Desktop_Qt_6_11_1_MinGW_64_bit_Debug` — an underscore is missing before `bit`). Still points at
  the MinGW build, not MSVC — the `Visual Studio 17 2022` generator used for the MSVC build is a
  multi-config MSBuild generator and doesn't support `CMAKE_EXPORT_COMPILE_COMMANDS`, so there's no
  `compile_commands.json` to point clangd at there. Fixing this properly means either configuring an
  additional Ninja-generator MSVC build just for `compile_commands.json` (needs a
  vcvars-initialized shell, unlike the VS generator which handles that itself), or switching clangd
  to a different mechanism entirely. Unresolved — clangd/IntelliSense for the MSVC+SDK code will lag
  until this is addressed.

The SDK is linked in but not yet used from application code (`main.cpp`/`Main.qml` are still
Hello-World boilerplate). For Phase 1 (file listing), expect a new C++ layer between the QML UI and
the SDK's C++ API (`megaapi.h`'s `MegaApi`/`MegaListener`/`MegaNode` etc. — see
`third_party/sdk/examples/simple_client/simple_client.cpp` for the basic login/fetchNodes call
pattern already validated in Phase 0), since the SDK is not QML-friendly out of the box.
