# CLAUDE.md

Guidance for Claude Code when working in this repo. Full feature list/roadmap detail lives in
`MEMO.md` (Japanese) — `README.md` is just a one-line title stub, not documentation.

## Project status

- **Phase 0 done**: MEGA SDK (`meganz/sdk` v10.17.0) + `vcpkg` vendored as submodules under
  `third_party/`; `appMegaExplorer` links `MEGA::SDKlib`; CLI login/fetchNodes verified.
- **Phase 1 next** (bare file listing): `main.cpp`/`Main.qml` are still stock Qt Creator
  boilerplate — no MEGA UI wiring exists yet. Check current file contents before assuming a
  feature exists; don't trust the roadmap alone.
- Roadmap (bottom-up, see `MEMO.md` for detail): 0 SDK build → **1 file listing → 2 thumbnails →
  3 search → 4 download/open/upload → 5 local cache + open-folder background refresh (= MVP)** →
  6 realtime remote-change reflection (post-MVP). Full bidirectional local sync is out of scope.

## What this project is

Windows-Explorer-like desktop client for MEGA cloud storage — thumbnail grid, search,
double-click-to-open (gaps in official MEGAsync). Not a sync client: only a one-shot background
refresh when a folder is opened, no continuous watching.

- No MEGA app key required (dropped 2023) — pass `nullptr` to `MegaApi`'s constructor
  (`meganz/sdk` issue #2706).
- Licensing: app is GPLv3 (via Qt), MEGA SDK is BSD-2-Clause. `meganz/MEGAsync`'s own source is
  under a restrictive Code Review Licence — never copy code from it, reference only for SDK usage
  patterns.
- Stack detail (thumbnail/cache libraries etc.) is in `MEMO.md`, not repeated here.

## Build

Qt 6.10+, CMake 3.20+ (SDK's own minimum). No `CMakePresets.json`.

**MSVC (VS2022) is the toolchain, not MinGW** — the SDK's Windows build is only
documented/supported for MSVC+vcpkg; MinGW is untested against its vcpkg dependency set. Qt is
installed at both `C:/Qt/6.11.1/mingw_64` (legacy, pre-SDK, unused) and
`C:/Qt/6.11.1/msvc2022_64` (current).

Submodules: `third_party/sdk` (pinned `v10.17.0`), `third_party/vcpkg` (**full history — do not
shallow-clone**, its baseline resolution needs `git show` on historical `versions/baseline.json`
blobs). Fresh clone: `git submodule update --init --recursive`, then
`third_party/vcpkg/bootstrap-vcpkg.bat`.

Reconfigure/build (MSVC):
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
The `VCPKG_*` flags exist because the SDK's CMake only auto-configures vcpkg when it's the
top-level project; embedded via our `add_subdirectory`, we must replicate that manually. Feature
list mirrors the SDK's own Windows defaults — check `third_party/sdk/cmake/modules/sdklib_options.cmake`
if the SDK version bumps and defaults change.

**Build only `appMegaExplorer`, not the full solution**: the SDK's `gfxworker` tool currently fails
to link (`LNK2019`, unresolved FFmpeg `sws_*`) in this config — unrelated, not yet root-caused.
Revisit before Phase 2 if isolated GFX processing is needed.

Binary: `build/Desktop_Qt_6_11_1_MSVC2022_64bit_Debug/Debug/appMegaExplorer.exe`. Needs Qt's `bin`
and vcpkg's `debug/bin` on `PATH` to run outside Qt Creator:
```
set PATH=C:\Qt\6.11.1\msvc2022_64\bin;%CD%\build\Desktop_Qt_6_11_1_MSVC2022_64bit_Debug\vcpkg_installed\x64-windows-mega\debug\bin;%PATH%
```

To sanity-check the SDK in isolation (e.g. after bumping the pinned version):
```
cmake -S third_party/sdk -B build/sdk-msvc-debug -G "Visual Studio 17 2022" -A x64 ^
    -DVCPKG_ROOT=third_party/vcpkg -DCMAKE_BUILD_TYPE=Debug -DENABLE_SDKLIB_TESTS=OFF -DENABLE_SDKLIB_WERROR=OFF
cmake --build build/sdk-msvc-debug --config Debug
```
(`ENABLE_SDKLIB_WERROR=OFF` avoids `C4819` — a harmless non-English-codepage warning — being
promoted to an error, which only happens when SDKlib is standalone/top-level.) Produces
`build/sdk-msvc-debug/examples/simple_client/Debug/simple_client.exe`, which logs in and lists the
root folder given `MEGA_EMAIL`/`MEGA_PWD` env vars — useful for isolating "SDK/vcpkg build" vs.
"our CMake wiring" when something breaks.

No test target, linter, or CI yet — see "Design" below for the testing plan.

## Architecture

- `main.cpp` — `QGuiApplication`/`QQmlApplicationEngine` bootstrap; loads the `MegaExplorer` QML
  module's `Main` component via `loadFromModule`.
- `Main.qml` — root `ApplicationWindow`. QML files are registered in `CMakeLists.txt`'s
  `qt_add_qml_module(... QML_FILES ...)` — new `.qml` files must be added there.
- `importedcontent/` — Figma-to-Qt export drop-in; auto-`add_subdirectory`'d if its
  `CMakeLists.txt` exists. Currently empty.
- `third_party/sdk` — `meganz/sdk` submodule, exposes `MEGA::SDKlib`. Vendored, do not edit.
- `third_party/vcpkg` — builds the SDK's third-party deps per `third_party/sdk/vcpkg.json`.
- `build/` — out-of-source, regeneratable. Multiple dirs exist: stale MinGW one, the current MSVC
  one, and `sdk-msvc-debug` (standalone SDK validation).
- `.clangd` — stale, points at a MinGW compile-commands path that doesn't match any real build dir
  (typo + predates the SDK/MSVC switch). Not fixed for MSVC either: the VS-generator build can't
  emit `compile_commands.json`. clangd/IntelliSense currently lags for MSVC+SDK code.

The SDK is linked but unused from application code so far. Phase 1 needs a new C++ layer between
QML and the SDK's C++ API (`megaapi.h`'s `MegaApi`/`MegaListener`/`MegaNode` — see
`third_party/sdk/examples/simple_client/simple_client.cpp` for the login/fetchNodes pattern already
validated). That layer must follow the ports-and-adapters design below, not call
`MegaApi`/`std::filesystem` directly.

## Design: testability and dependency injection

Decided 2026-07-24, before Phase 1. C++ has no reflection-based DI container in mainstream use —
use manual constructor injection against abstract interfaces, wired at a **composition root**.
Do not add a DI framework (Boost.DI, Fruit, etc.) — unneeded complexity at this project's size.

- **Ports**: `IMegaClient` (wraps `mega::MegaApi`), `IFileSystem` (wraps `std::filesystem`).
  Domain/UI code depends only on these, never on SDK or filesystem types directly.
- **Adapters**: `MegaSdkClient`, `RealFileSystem` — the only code allowed to include `megaapi.h`
  or call `std::filesystem` directly.
- **Composition root**: `main.cpp` `make_shared`s the concrete adapters and injects them via
  constructor (`std::shared_ptr<IPort>`). No service locator, no container.
- **Async seam**: `MegaApi` is listener/callback-based (see `MegaListener::onRequestFinish` in
  `simple_client.cpp`) — `IMegaClient` methods take a completion callback
  (`std::function<void(Result<...>)>`), not a synchronous return, so test fakes can simulate
  success/failure/timeout.
- **Testing**: GoogleTest/GoogleMock (not yet added — pull via vcpkg alongside Phase 1).
  `MOCK_METHOD` fakes for `IMegaClient`/`IFileSystem` let file-listing logic be unit-tested without
  a real MEGA login.
- **Planned layout** (not yet created):
  ```
  src/core/      domain logic; depends only on IMegaClient/IFileSystem, never MEGA SDK headers
  src/mega/      MegaSdkClient adapter; only place allowed to include megaapi.h
  src/platform/  RealFileSystem adapter
  src/qml/       C++ types exposed to QML (Q_PROPERTY etc.)
  tests/         GoogleTest-based unit tests
  ```
