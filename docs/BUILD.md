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
variables) into one named preset, `msvc-debug`. Added because Qt Creator's per-row CMake
configuration GUI proved unreliable for this many variables — batch-pasted entries silently failed
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

Preferred: the `qtcreator` MCP server (Qt Creator's Extensions > MCP Server, enabled in
Preferences > AI > Qt Creator MCP Server — must be running, registered locally via `claude mcp add
--transport http qtcreator http://127.0.0.1:<port>/ --scope local`, not committed). Call
`mcp__qtcreator__build` (or `list_issues`/`list_file_issues`); its `issues` array returns `{file,
line, description, type}` per diagnostic with an absolute path, so filtering out `third_party/sdk`
is a reliable path-prefix check rather than a text-based `grep -v`. Confirmed 2026-07-24: warnings
raised only on the `appMegaExplorer` target (e.g. via its own `/W4`) don't leak SDKlib/third_party
noise into the array at all, since those are separate CMake targets.

`/W4` is scoped to `appMegaExplorer` only (`target_compile_options(appMegaExplorer PRIVATE /W4)`
in root `CMakeLists.txt`) so `third_party/sdk` isn't affected by the stricter level.
