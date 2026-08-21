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
directory name: `build/msvc-debug/Release/appMegaExplorer.exe` is a Release binary living under a
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

## Why only `appMegaExplorer`, not the full solution

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
directly, so **without this fix `appMegaExplorer` itself fails at final link** (`LNK2019` on the
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

`find_package(Qt6 ...)` needs `Gui` for it, and `appMegaExplorer` links `QWindowKit::Quick`.
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

`/W4` reaches all six of our targets — `MegaExplorerCore`, `MegaExplorerQml`, `appMegaExplorer`,
`megatool`, `MegaExplorerTests`, `MegaExplorerQmlTests` — through the `MegaExplorerWarnings`
interface target they each link `PRIVATE`. `PRIVATE` is what keeps it off `third_party/sdk` and QWindowKit; putting `/W4` in
`CMAKE_CXX_FLAGS` instead would hit everything and is why the flag was target-scoped from the
start. Before R4-9 it was on `appMegaExplorer` alone, which meant `src/core` and all of `tests/`
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
