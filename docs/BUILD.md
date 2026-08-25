# Build: full rationale

The commands you actually run day to day are in `CLAUDE.md`'s "Build" section. This file has the
*why* behind each gotcha — read it when a build breaks in a way the short version doesn't explain,
or before changing any of the CMake wiring it documents.

## Why the Visual Studio generator, not Ninja

`third_party/sdk/cmake/modules/sdklib_variables.cmake:11` unconditionally sets
`CMAKE_GENERATOR_TOOLSET "v142"` on Windows, and `CMAKE_GENERATOR_TOOLSET` only has meaning for
Visual Studio generators; configuring with Ninja fails outright (`Generator Ninja does not support
toolset specification`) no matter what other variables are set. This is vendored SDK code (do not
edit) — Qt Creator kits/CMake presets must select `Visual Studio 17 2022` as the generator.

## Why `CMakePresets.json` exists

Pins the full configure (generator, architecture, vcpkg toolchain file, and all `VCPKG_*`
variables) into one named preset, `msvc-debug` — one *configure* preset, with two build presets
(`msvc-debug`, `msvc-release`) hanging off it; see "Debug vs Release" below. Added because Qt
Creator's per-row CMake configuration GUI proved unreliable for this many variables — batch-pasted
entries silently failed
to apply, and individually-added entries (specifically `CMAKE_TOOLCHAIN_FILE`) were dropped on the
next "Run CMake". Qt Creator auto-detects presets from this file and lists them in the kit/build
configuration picker — select `msvc-debug` there instead of hand-entering variables. The manual
`-D`-flag invocation in `CLAUDE.md` still works and documents the same variables explicitly; keep
both in sync if one changes.

The `VCPKG_*` flags exist because the SDK's CMake only auto-configures vcpkg when it's the
top-level project; embedded via our `add_subdirectory`, we must replicate that manually. The
feature list (`use-openssl;use-freeimage;use-ffmpeg;use-pdfium;sdk-tests`) mirrors the SDK's own
Windows defaults — check `third_party/sdk/cmake/modules/sdklib_options.cmake` if the SDK version
bumps and defaults change. `sdk-tests` is `third_party/sdk/vcpkg.json`'s own feature name for
pulling in `gtest` — it only affects what vcpkg installs, unrelated to the SDK's own
`ENABLE_SDKLIB_TESTS` option (still off).

## Debug vs Release

There is one configure preset, not two. The Visual Studio generator is multi-config, so
`--build --preset msvc-release` builds the `Release` configuration out of the very same tree and
`vcpkg_installed` directory that `msvc-debug` configured — vcpkg installs both the release and the
debug variant of every dependency on first configure, so a Release build needs no second install
(which would mean rebuilding ffmpeg, pdfium and OpenSSL from source). The cost of that reuse is the
directory name: `build/msvc-debug/Release/MegaExplorer.exe` is a Release binary living under a
path named for the *configure* preset. A second binary directory would buy a tidier name for hours
of vcpkg time.

At runtime the release binary needs `vcpkg_installed/x64-windows-mega/bin` on `PATH`, not
`debug/bin`.

**Debug timings mean nothing.** Folder navigation is faster in Release by a margin obvious without a
stopwatch, and none of it is our code being different — MSVC's Debug defaults are.
`_ITERATOR_DEBUG_LEVEL=2` puts checked-iterator bookkeeping on every `std::vector`/`std::string`
operation, and the listing path is exactly that: the `MegaNodeList` -> `std::vector<FileEntry>`
conversion allocates several strings per row. `/Od` removes inlining from the thin `IMegaClient`
wrapper layer, the debug CRT heap guards and fills every one of those allocations, and both the SDK
(node-attribute decryption) and Qt's QML engine run their own debug builds underneath. Reproduce in
Release before treating anything as a performance problem.

## Why only `MegaExplorer`, not the full solution

The SDK's `gfxworker` tool currently fails to link (`LNK2019`, unresolved FFmpeg `sws_*`) in this
config — unrelated to our own code, not yet root-caused. Revisit if isolated GFX processing is
needed.

## FFmpeg `swscale` link fix (root `CMakeLists.txt`)

`SDKlib`'s own FFmpeg lookup (`third_party/sdk/cmake/modules/sdklib_libraries.cmake`) calls
`find_package(FFMPEG REQUIRED)` with no `COMPONENTS`, which resolves through Qt's
`FindFFmpeg.cmake` (found ahead of vcpkg's FFmpeg config on the module path) — that module only
searches `AVCODEC`/`AVFORMAT`/`AVUTIL` by default, so `FFmpeg::swscale` is never created even
though vcpkg builds `swscale.lib`. `SDKlib`'s FreeImage backend
(`GfxProviderFreeImage::readbitmapFfmpeg`) calls `sws_getContext`/`sws_scale`/`sws_freeContext`
directly, so **without this fix `MegaExplorer` itself fails at final link** (`LNK2019` on the
three `sws_*` symbols), not just `gfxworker` — this was found while confirming a clean link is
achievable for Phase 1's C++ layer. Root `CMakeLists.txt` re-requests the component and links it
into `SDKlib` after `add_subdirectory(third_party/sdk)`:

```cmake
find_package(FFmpeg COMPONENTS SWSCALE)
if(TARGET FFmpeg::swscale)
    target_link_libraries(SDKlib PRIVATE FFmpeg::swscale)
endif()
```

`third_party/sdk` itself is untouched (vendored, do not edit) — this patches the link from our own
top-level `CMakeLists.txt`. If the SDK version bumps and `sdklib_libraries.cmake` starts requesting
`SWSCALE` itself, this block becomes a no-op and can be removed.

## QWindowKit (`third_party/qwindowkit`, Phase 17a)

Vendored submodule pinned to tag `1.5.0`, supplying the frameless-window/caption handling behind
`qml/components/CaptionBar.qml`. Two things about it differ from the other two submodules:

- **It is not shallow** (`third_party/sdk` is), and it has a *nested* submodule of its own, `qmsetup`
  — which nests `syscmdline` in turn. `git submodule update --init --recursive` is therefore not
  optional here; a non-recursive init leaves QWindowKit's own configure step unable to find
  `qmsetup` and the whole configure fails.
- Its root `CMakeLists.txt` **configures and builds `qmsetup` as a separate host project at
  configure time** (`qm_install_package(qmsetup HOST ...)`). This was expected to fight the VS
  generator + vcpkg toolchain setup and does not; nothing has to be pre-built or passed via
  `QWindowKit_DIR`.

Four cache variables are forced in the root `CMakeLists.txt` before `add_subdirectory`:

```cmake
set(QWINDOWKIT_BUILD_QUICK   ON  CACHE BOOL "" FORCE)
set(QWINDOWKIT_BUILD_WIDGETS OFF CACHE BOOL "" FORCE)   # app is QGuiApplication, no QtWidgets
set(QWINDOWKIT_BUILD_STATIC  ON  CACHE BOOL "" FORCE)   # keeps a DLL off the runtime PATH
set(QWINDOWKIT_INSTALL       OFF CACHE BOOL "" FORCE)
```

`find_package(Qt6 ...)` needs `Gui` for it, and `MegaExplorer` links `QWindowKit::Quick`.
Configure logs `The CorePrivate target is mentioned as a dependency for QWindowKit::Quick, but not
declared` (likewise `GuiPrivate`/`QuickPrivate`) — cosmetic, from Qt 6.10+'s change to how private
modules are exported. Linking succeeds; do not chase it.

## SDK-only sanity check

To sanity-check the SDK in isolation (e.g. after bumping the pinned version), without our own
CMake wiring in the picture:

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

## Warning-check workflow detail

`scripts/loop_verify.sh` is the entry point (`CLAUDE.md`), and it must not grow an IDE dependency:
`/evolve` runs it unattended. What follows is the optional convenience for interactive sessions.

The `qtcreator` MCP server (Qt Creator's Extensions > MCP Server, enabled in
Preferences > AI > Qt Creator MCP Server — must be running, registered locally via `claude mcp add
--transport http qtcreator http://127.0.0.1:<port>/ --scope local`, not committed). Call
`mcp__qtcreator__build` (or `list_issues`/`list_file_issues`); its `issues` array returns `{file,
line, description, type}` per diagnostic with an absolute path, so filtering out `third_party/sdk`
is a reliable path-prefix check rather than a text-based `grep -v`. Confirmed 2026-07-24: warnings
raised only on our own targets (via `/W4`) don't leak SDKlib/third_party noise into the array at
all, since those are separate CMake targets.

`/W4` reaches all six of our targets — `MegaExplorerCore`, `MegaExplorerQml`, `MegaExplorer`,
`megatool`, `MegaExplorerTests`, `MegaExplorerQmlTests` — through the `MegaExplorerWarnings`
interface target they each link `PRIVATE`. `PRIVATE` is what keeps it off `third_party/sdk` and QWindowKit; putting `/W4` in
`CMAKE_CXX_FLAGS` instead would hit everything and is why the flag was target-scoped from the
start. Before R4-9 it was on `MegaExplorer` alone, which meant `src/core` and all of `tests/`
were never compiled at anything above MSVC's default `/W1`.

That target also carries two suppressions, both about *Qt's* headers rather than ours:

- `/external:W0` — CMake already passes Qt's and GTest's include directories as `/external:I`
  (imported targets are SYSTEM by default), but MSVC ignores that designation until an
  `/external:W` level is set. Without it, Qt headers instantiated from generated
  moc/qmlcachegen/type-registration code report as if they were our code.
- `/wd4702` (unreachable code) — the one warning `/external:W0` cannot reach, because it comes
  from the back end, which has no notion of external headers. A full rebuild produced 51 hits,
  every one inside `qjsengine.h`/`qvariant.h`/`qjsprimitivevalue.h` from qmlcachegen's AOT output
  and none in `src/`. These only appear on a *full* rebuild — incremental builds don't recompile
  the generated sources, which is why the sweep looked clean before R4-9.

## The distribution zip (`scripts/package.ps1`)

A build that runs here does not run anywhere else: outside Qt Creator the binary needs Qt's `bin`
and vcpkg's `debug/bin` on `PATH`, and on a machine without a Qt install there is nothing to put
there. Packaging is what turns that into a directory someone can unzip and double-click, and it is
one command: `scripts\package.ps1` builds Release, runs CPack's ZIP generator over the install
rules, and checks the archive before reporting success.

Four install rules feed it, and they are separate because nothing knows about all four:

- `install(TARGETS MegaExplorer)` — the exe.
- `install(FILES LICENSE THIRD-PARTY-NOTICES.txt)` — a licence obligation of shipping binaries at
  all, LGPL and BSD alike. The qrc copy behind `LicenseDialog` does not discharge it.
- `qt_generate_deploy_qml_app_script` + `install(SCRIPT)` — Qt's DLLs, its QML modules, the
  platform/imageformat/tls plugins, and the MSVC runtime, all via `windeployqt` at install time.
  Note *install* time: `cmake --build` alone never produces a runnable tree, which is the whole
  reason the packaging step is a script rather than a build target.
- A glob over `vcpkg_installed/<triplet>/bin/*.dll` — FFmpeg, and only FFmpeg. The triplet is
  static apart from it, and `windeployqt` cannot find it because it walks *Qt's* dependency graph
  while FFmpeg enters through SDKlib. Globbed rather than named per version so an FFmpeg bump
  cannot silently drop one, with a `FATAL_ERROR` on an empty result standing in for the compile
  error we would otherwise get.

`CMAKE_INSTALL_BINDIR` is pinned to `.` on Windows, before `include(GNUInstallDirs)`. That is what
flattens the layout — the exe, the DLLs and the two text files at the archive root, with only
`plugins/` and `qml/` below — because `windeployqt` takes its own destination from
`QT_DEPLOY_BIN_DIR`, which Qt generates from `CMAKE_INSTALL_BINDIR`. It has to be a **normal**
variable, not a cache one: a `bin` left in `CMakeCache.txt` by an earlier configure wins over a
non-`FORCE` `set(... CACHE ...)`, and the symptom is a zip that works but buries everything one
directory deep.

The script's closing check — that `MegaExplorer.exe`, `qt.conf`, `Qt6Core.dll`, `Qt6Quick.dll`,
`avcodec-61.dll`, `platforms/qwindows.dll`, `qml/QtQuick/qmldir`, `LICENSE` and
`THIRD-PARTY-NOTICES.txt` are all present — is not ceremony. Every failure this exists to catch
produces a zip that configures, builds and packages without a word and then dies on the receiving
desktop, `qwindows.dll` with "no Qt platform plugin could be initialized" and the rest with a
missing-DLL box.

`-Config Debug` packages too, and is useful for checking the layout without a Release build, but
the result must never be shipped: it carries the debug CRT, which is not redistributable.
