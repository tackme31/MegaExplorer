# CLAUDE.md

Guidance for Claude Code when working in this repo. Full feature list/roadmap detail lives in
`MEMO.md` (Japanese) — `README.md` is just a one-line title stub, not documentation.

## Project status

- **Phase 0 done**: MEGA SDK (`meganz/sdk` v10.17.0) + `vcpkg` vendored as submodules under
  `third_party/`; `appMegaExplorer` links `MEGA::SDKlib`; CLI login/fetchNodes verified.
- **Phase 1 done** (bare file listing): `IMegaClient`/`MegaSdkClient` (login/fetchNodes/
  getRootChildren) under `src/core`/`src/mega`; `FileListingService` (`src/core`) chains
  login→fetchNodes→getRootChildren; `FileListModel` (`src/qml`) bridges results into QML. Covered
  by `MockMegaClient`-based tests (`tests/FileListingServiceTest.cpp`).
- **Phase 2 done** (folder navigation): `FolderNavigationService` (`src/core`) adds
  `openFolder`/`goBack` over `IMegaClient::getChildren`, back-stack-based, with the root modeled as
  a sentinel `Location` (no real handle for root). `FolderNavigationController` (`src/qml`) wraps
  both `FileListingService` (initial root load) and `FolderNavigationService` (subsequent
  navigation) and is registered as the `controller` context property in `main.cpp`, replacing the
  standalone `FileListModel` wiring. `Main.qml` has a real `ListView` (delegate shows folder/file
  icon + name), double-click-to-open on folders via `TapHandler.onDoubleTapped`, and a header
  `ToolBar` with a Back button gated on `controller.canGoBack`. Covered by
  `tests/FolderNavigationServiceTest.cpp`. Known rough edges (deferred, not blocking Phase 3):
  `FolderNavigationController::applyResult` only `qWarning()`s on failure with no UI feedback, and
  there's no breadcrumb/current-path display.
- **Phase 3 done** (search): MVP scope decided 2026-07-25 — a search box in the header, default
  behavior is a **recursive search scoped to the currently open folder** (`MegaSearchFilter::byName`
  + `byLocationHandle(currentFolderHandle)` via `MegaApi::search()`, which is a synchronous call,
  not listener/callback-based like login/fetchNodes). `IMegaClient::search` (`src/core`/`src/mega`)
  mirrors `FolderNavigationService`'s root-sentinel `Location` convention; `SearchService`
  (`src/core`) resolves the scope via a new `FolderNavigationService::currentLocation()` getter and
  is SDK-free/unit-tested like the other core services (`tests/SearchServiceTest.cpp`).
  `FolderNavigationController::search(QString)` runs it and swaps `FileListModel`'s contents without
  touching navigation state (back-stack/`canGoBack`); an empty query restores the last folder
  listing from a cached member instead of re-fetching. `Main.qml` has a `TextField` in the header
  `ToolBar`, submitting on Enter only (`MegaApi::search()` blocks the GUI thread synchronously).
  A filter button beside the search box, deferred to a later pass, will expose the rest of
  `MegaSearchFilter` (account-wide search via `byLocation(SEARCH_TARGET_ALL)`, `byCategory`,
  `byCreationTime`/`byModificationTime`, `byFavourite`, `bySensitivity`, `byTag`/`byDescription`)
  plus `MegaSearchPage`-based pagination — none of that was needed for the MVP search box itself.
  Same known rough edge as Phase 2: search failure only `qWarning()`s, no UI feedback.
- **Phase 4 done** (download → open): hybrid flow — file double-click and a new right-click context
  menu's "ダウンロード" item both call `DownloadController::downloadFile(handle, name, sizeBytes)`
  (`src/qml`, new `downloadController` context property, separate from `FolderNavigationController`
  since it's an independent concern), so they funnel into the same flow instead of diverging.
  `IMegaClient::download` (`src/core`/`src/mega`) diverges from the rest of the interface's single-
  `Result<T>`-callback shape: `MegaApi::startDownload` is `MegaTransferListener`-based, so it takes a
  progress callback plus a `Result<DownloadOutcome>` completion callback (`DownloadOutcome`,
  `src/core/DownloadOutcome.h`: `localPath` is the actual saved path, since
  `COLLISION_RESOLUTION_NEW_WITH_N` can rename on a name collision; `alreadyPresent` is true when an
  identical — fingerprint-matching — file already existed at the destination and the SDK skipped the
  transfer outright rather than writing anything, per `COLLISION_CHECK_FINGERPRINT`). Confirmed by
  tracing `third_party/sdk`'s collision-resolution path (`megaapi_impl.cpp`'s `CollisionChecker`/
  `CompleteFileDownloadBySkip`, `filesystem.cpp`'s `FileDistributor::moveToForMethod_RenameWithBracketedNumber`,
  which calls Win32 `MoveFileExW` *without* `MOVEFILE_REPLACE_EXISTING`) that this combination never
  actually overwrites a same-named file with different content — it renames the new file with a
  `(1)`/`(2)`/... suffix instead. `alreadyPresent` is inferred in `MegaSdkClient`'s `DownloadListener`
  (no public SDK getter exists for the collision-check outcome) from
  `transfer->getTransferredBytes() == 0 && transfer->getTotalBytes() > 0` at completion, since
  `CompleteFileDownloadBySkip` explicitly zeroes `transferredBytes` on the skip path. Added
  2026-07-25 after a user report of "same-name downloads overwrite the existing file" turned out to
  be this skip case surfacing no visible difference in the UI (`DownloadSnackbar.qml` showed the
  same generic "ダウンロード完了" message either way) rather than an actual overwrite — the snackbar now
  shows "既にダウンロード済みです" instead when `alreadyPresent` is true. The renamed-file case (different
  content, same requested name) is also covered: `DownloadController` reports the *actual* saved leaf
  name (`QFileInfo(resolvedLocalPath).fileName()`) as the snackbar's `fileName` on success, not the
  originally-requested `job.name` — so a collision-renamed download reads as e.g. "photo (1).jpg の
  ダウンロードが完了しました" instead of silently claiming "photo.jpg" completed, which is what read as an
  overwrite. `MegaSdkClient` implements it with a self-deleting `DownloadListener`, matching the existing
  `LoginListener`/`FetchNodesListener` idiom. `DownloadService` (`src/core`) serializes downloads one
  at a time over that port, auto-advancing the queue on completion; built with a per-job id/state
  (`DownloadJob`, `jobs()` snapshot) from the start so a future progress-list UI and per-job cancel
  can be added without an API change, even though this pass only surfaces the single active job to
  QML (`DownloadController`'s `downloadActive`/`activeFileName`/`activeProgress` properties, shown in
  a footer `ToolBar`). SDK-free/unit-tested like the other core services
  (`tests/DownloadServiceTest.cpp`); needed its own mutex (unlike the existing services) since
  `enqueue()` runs on the GUI thread while `IMegaClient::download`'s callbacks may fire on an
  SDK-internal background thread — `IMegaClient::download()` itself is deliberately called with no
  lock held, since `MockMegaClient`-based tests invoke its callbacks synchronously from that same
  call. On completion, `DownloadSnackbar.qml` shows a "開く" button on success
  (`DownloadController::openFile` → `QDesktopServices::openUrl`, no auto-open — same "don't surprise
  the user with something launching mid-interaction" reasoning as search-on-Enter) or the error
  message on failure; unlike Phase 2/3, download failures *do* surface in the UI, since a download is
  an intentionally-waited-for action rather than a background navigation/search call. Files save to
  the platform's real Downloads folder (`QStandardPaths::DownloadLocation`, no app-specific
  subfolder — matches ordinary browser download behavior; revised 2026-07-25 during implementation
  from an initial temp-folder choice). Gotcha found during manual verification: destination paths
  must be nativized via `QDir::toNativeSeparators()` before reaching `MegaApi::startDownload` — the
  SDK's own `LocalPath`/`Path` (`third_party/sdk/src/localpath.cpp`) splits on `\` specifically on
  Windows, so a `/`-separated path (Qt's own convention) was treated as one giant leaf name including
  the drive letter, tripping an internal invariant `assert()`. Format-specific in-app preview
  (`MegaApi::getPreview`/`startStreaming` — considered and explicitly deferred, see `MEMO.md`) and
  upload remain out of scope.
- Check current file contents before assuming a feature exists; don't trust the roadmap alone.
- Roadmap (bottom-up, see `MEMO.md` for detail): 0 SDK build → 1 file listing → 2 folder
  navigation (double-click into subfolders) → 3 search → 4 download/open → **5 thumbnails** →
  6 local cache + open-folder background refresh (= MVP) → 7 realtime remote-change reflection
  (post-MVP). Thumbnails/preview deprioritized 2026-07-24 — not required for the app to be usable.
  Full bidirectional local sync is out of scope; upload has no assigned phase yet (see MEMO.md's
  feature list).

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

Qt 6.10+, CMake 3.20+ (SDK's own minimum).

**MSVC (VS2022) is the toolchain, not MinGW** — the SDK's Windows build is only
documented/supported for MSVC+vcpkg; MinGW is untested against its vcpkg dependency set. Qt is
installed at both `C:/Qt/6.11.1/mingw_64` (legacy, pre-SDK, unused) and
`C:/Qt/6.11.1/msvc2022_64` (current).

**Must use the Visual Studio generator, not Ninja** — regardless of IDE/kit.
`third_party/sdk/cmake/modules/sdklib_variables.cmake:11` unconditionally sets
`CMAKE_GENERATOR_TOOLSET "v142"` on Windows, and `CMAKE_GENERATOR_TOOLSET` only has meaning for
Visual Studio generators; configuring with Ninja fails outright (`Generator Ninja does not support
toolset specification`) no matter what other variables are set. This is vendored SDK code (do not
edit) — Qt Creator kits/CMake presets must select `Visual Studio 17 2022` as the generator.

**`CMakePresets.json`** pins the full configure (generator, architecture, vcpkg toolchain file, and
all `VCPKG_*` variables below) into one named preset, `msvc-debug`. Added because Qt Creator's
per-row CMake configuration GUI proved unreliable for this many variables — batch-pasted entries
silently failed to apply, and individually-added entries (specifically `CMAKE_TOOLCHAIN_FILE`) were
dropped on the next "Run CMake". Qt Creator auto-detects presets from this file and lists them in
the kit/build configuration picker — select `msvc-debug` there instead of hand-entering variables.
From the CLI: `cmake --preset msvc-debug` / `cmake --build --preset msvc-debug`. The manual
`-D`-flag invocation below still works and documents the same variables explicitly; keep both in
sync if one changes.

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
    -DVCPKG_MANIFEST_FEATURES="use-openssl;use-freeimage;use-ffmpeg;use-pdfium;sdk-tests"
cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit_Debug --config Debug --target appMegaExplorer
```
`sdk-tests` is `third_party/sdk/vcpkg.json`'s own feature name for pulling in `gtest` — it only
affects what vcpkg installs, unrelated to the SDK's own `ENABLE_SDKLIB_TESTS` option (still off).
Build the test target the same way, with `--target MegaExplorerTests` instead.
The `VCPKG_*` flags exist because the SDK's CMake only auto-configures vcpkg when it's the
top-level project; embedded via our `add_subdirectory`, we must replicate that manually. Feature
list mirrors the SDK's own Windows defaults — check `third_party/sdk/cmake/modules/sdklib_options.cmake`
if the SDK version bumps and defaults change.

**Build only `appMegaExplorer`, not the full solution**: the SDK's `gfxworker` tool currently fails
to link (`LNK2019`, unresolved FFmpeg `sws_*`) in this config — unrelated, not yet root-caused.
Revisit before Phase 2 if isolated GFX processing is needed.

**FFmpeg `swscale` link fix (root CMakeLists.txt)**: `SDKlib`'s own FFmpeg lookup
(`third_party/sdk/cmake/modules/sdklib_libraries.cmake`) calls `find_package(FFMPEG REQUIRED)` with
no `COMPONENTS`, which resolves through Qt's `FindFFmpeg.cmake` (found ahead of vcpkg's FFmpeg
config on the module path) — that module only searches `AVCODEC`/`AVFORMAT`/`AVUTIL` by default, so
`FFmpeg::swscale` is never created even though vcpkg builds `swscale.lib`. `SDKlib`'s FreeImage
backend (`GfxProviderFreeImage::readbitmapFfmpeg`) calls `sws_getContext`/`sws_scale`/
`sws_freeContext` directly, so **without this fix `appMegaExplorer` itself fails at final link**
(`LNK2019` on the three `sws_*` symbols), not just `gfxworker` — this was found while confirming a
clean link is achievable for Phase 1's C++ layer. Root CMakeLists.txt re-requests the component and
links it into `SDKlib` after `add_subdirectory(third_party/sdk)`:
```cmake
find_package(FFmpeg COMPONENTS SWSCALE)
if(TARGET FFmpeg::swscale)
    target_link_libraries(SDKlib PRIVATE FFmpeg::swscale)
endif()
```
`third_party/sdk` itself is untouched (vendored, do not edit) — this patches the link from our own
top-level CMakeLists.txt. If the SDK version bumps and `sdklib_libraries.cmake` starts requesting
`SWSCALE` itself, this block becomes a no-op and can be removed.

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

**Compiler warnings**: `appMegaExplorer` builds at `/W4` (`target_compile_options(appMegaExplorer
PRIVATE /W4)` in root CMakeLists.txt, scoped to that target only so `third_party/sdk` isn't
affected by the stricter level). At the end of any task touching `main.cpp`/`src/`, check for new
warnings and fix them before considering the task done.

Preferred: the `qtcreator` MCP server (Qt Creator's Extensions > MCP Server, enabled in
Preferences > AI > Qt Creator MCP Server — must be running, registered locally via
`claude mcp add --transport http qtcreator http://127.0.0.1:<port>/ --scope local`, not
committed). Call `mcp__qtcreator__build` (or `list_issues`/`list_file_issues`); its `issues` array
returns `{file, line, description, type}` per diagnostic with an absolute path, so filtering out
`third_party/sdk` is a reliable path-prefix check rather than a text-based `grep -v`. Confirmed
2026-07-24: warnings raised only on the `appMegaExplorer` target (e.g. via its own `/W4`) don't
leak SDKlib/third_party noise into the array at all, since those are separate CMake targets.

CLI-only fallback (no Qt Creator running, or the MCP port has changed):
```
C:/Qt/Tools/CMake_64/bin/cmake.exe --build build/msvc-debug --config Debug --target appMegaExplorer 2>&1 | grep -i "warning C" | grep -v "third_party"
```
Full path to `cmake.exe` is required — the `cmake` on `PATH` resolves to Strawberry Perl's copy,
which is unrelated and wrong for this project.

**Tests**: `MegaExplorerTests` (GoogleTest/GoogleMock, `tests/`) is registered with CTest via
`gtest_discover_tests`. Run with `ctest --preset msvc-debug` (after building the target) or execute
`build/msvc-debug/tests/Debug/MegaExplorerTests.exe` directly. No linter or CI yet — see "Design"
below for what the tests cover.

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
  one, `msvc-debug` (via `CMakePresets.json`'s `msvc-debug` preset, used by Qt Creator), and
  `sdk-msvc-debug` (standalone SDK validation).
- `CMakePresets.json` — the `msvc-debug` preset; see "Build" above for why it exists.
- `.clangd` — stale, points at a MinGW compile-commands path that doesn't match any real build dir
  (typo + predates the SDK/MSVC switch). Not fixed for MSVC either: the VS-generator build can't
  emit `compile_commands.json`. clangd/IntelliSense currently lags for MSVC+SDK code.

`src/core`/`src/mega` (see Design below) wrap the SDK's C++ API (`megaapi.h`'s
`MegaApi`/`MegaRequestListener`/`MegaNode` — see
`third_party/sdk/examples/simple_client/simple_client.cpp` for the login/fetchNodes pattern this
was validated against). `main.cpp` is the composition root: it builds a `MegaSdkClient`, hands it
to a `FileListingService`, and logs the result via `qDebug()` — still no QML consumer. Any further
MEGA-facing code must go through `IMegaClient`/`FileEntry`, never call `MegaApi`/`std::filesystem`
directly (ports-and-adapters design below).

## Design: testability and dependency injection

Decided 2026-07-24, before Phase 1. C++ has no reflection-based DI container in mainstream use —
use manual constructor injection against abstract interfaces, wired at a **composition root**.
Do not add a DI framework (Boost.DI, Fruit, etc.) — unneeded complexity at this project's size.

- **Ports**: `IMegaClient` (wraps `mega::MegaApi`, `src/core/IMegaClient.h`) — done: `login`,
  `fetchNodes`, `getRootChildren` (returns `std::vector<FileEntry>`, `src/core/FileEntry.h`).
  `IFileSystem` (wraps `std::filesystem`) — not yet added, needed once local caching starts
  (roadmap step 5). Domain/UI code depends only on these, never on SDK or filesystem types
  directly.
- **Adapters**: `MegaSdkClient` (`src/mega/MegaSdkClient.h`/`.cpp`) — done, implements
  `IMegaClient` over `mega::MegaApi` using a per-call, self-deleting `MegaRequestListener`
  subclass (`new Listener(...)` passed to `login()`/`fetchNodes()`, `delete this` at the end of
  `onRequestFinish`). `RealFileSystem` — not yet added. `MegaSdkClient.cpp`/`.h` are the only
  files allowed to include `megaapi.h` or call `std::filesystem` directly.
- **`Result<T>`** (`src/core/Result.h`): hand-rolled success/failure wrapper (no `std::expected` —
  `CMAKE_CXX_STANDARD` isn't pinned in this project, so C++23 can't be assumed); has a `Result<void>`
  specialization for callbacks with no payload (e.g. `login`).
- **Composition root**: `main.cpp` `make_shared`s the concrete adapters and injects them via
  constructor (`std::shared_ptr<IPort>`). No service locator, no container. Done: builds a
  `MegaSdkClient`, injects it into a `FileListingService`, and logs the result.
- **Domain logic**: `FileListingService` (`src/core/FileListingService.h`/`.cpp`) — first piece of
  domain logic, depends only on `IMegaClient`. Chains `login`→`fetchNodes`→`getRootChildren`,
  short-circuiting to a failed `Result<std::vector<FileEntry>>` if any stage fails. SDK-free, so
  it's unit-testable with a mocked `IMegaClient` (see Testing below).
- **Async seam**: `MegaApi` is listener/callback-based (see `MegaListener::onRequestFinish` in
  `simple_client.cpp`) — `IMegaClient` methods take a completion callback
  (`std::function<void(Result<...>)>`), not a synchronous return, so test fakes can simulate
  success/failure/timeout. Note `login`/`fetchNodes` callbacks fire on an SDK-internal background
  thread (confirmed by `simple_client.cpp`'s own comment); `getRootChildren` is synchronous
  under the hood (`MegaApi::getRootNode()`/`getChildren()`, not request/listener-based) but kept
  callback-shaped for interface consistency — this asymmetry isn't addressed by any Qt-thread
  marshaling yet, since there's no QML consumer.
- **Testing**: GoogleTest/GoogleMock — done, pulled via vcpkg's `sdk-tests` feature (see Build
  above). `MockMegaClient` (`tests/FileListingServiceTest.cpp`) `MOCK_METHOD`-fakes `IMegaClient`
  and drives its callback args with `testing::InvokeArgument`, covering `FileListingService`'s
  success path and both short-circuit-on-failure paths, without a real MEGA login. `IFileSystem`
  has no fake yet (not added).
- **`MegaExplorerCore`**: a static library target (root `CMakeLists.txt`) bundling `src/core`'s
  SDK-free headers plus `FileListingService.cpp`. Exists so `appMegaExplorer` and
  `MegaExplorerTests` link the same compiled domain logic instead of each recompiling it standalone.
- **Layout**:
  ```
  src/core/      IMegaClient.h, FileEntry.h, Result.h, FileListingService.{h,cpp} — done
  src/mega/      MegaSdkClient adapter — done; only place allowed to include megaapi.h
  src/platform/  RealFileSystem adapter — not yet created
  src/qml/       C++ types exposed to QML (Q_PROPERTY etc.) — not yet created
  tests/         GoogleTest-based unit tests — done: FileListingServiceTest.cpp
  ```
