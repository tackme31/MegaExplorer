# CLAUDE.md

Guidance for Claude Code when working in this repo. Kept compact — detail that isn't needed every
session lives in companion docs, linked from the relevant section below rather than inlined:

- `MEMO.md` — feature list / roadmap detail (Japanese). `README.md` is just a one-line title
  stub, not documentation.
- `TASKS.md` — current phase's task checklist, present while a phase is in progress.
- `docs/PROGRESS.md` — full phase-by-phase implementation log (what was built, why, gotchas).
- `docs/ARCHITECTURE.md` — directory layout detail + the ports-and-adapters/DI design.
- `docs/BUILD.md` — rationale behind each build gotcha below (why VS generator, why
  `CMakePresets.json`, the FFmpeg link fix, etc.).
- `docs/TROUBLESHOOTING.md` — recurring environment issues (e.g. stale git `index.lock`).

Check current file contents before assuming a feature exists — don't trust the roadmap alone.

## Tooling: use Serena MCP proactively

Serena MCP (`mcp__serena__*`) is available and should be the default way to navigate/edit this
codebase — prefer it over `Read`ing whole files or shelling out to `grep`/`find`, to keep token
usage down. Use `get_symbols_overview`/`find_symbol` to locate classes/methods instead of reading
entire `.h`/`.cpp` files top to bottom, `find_referencing_symbols` instead of a broad text search
when tracing callers, and `replace_symbol_body`/`insert_after_symbol`/`insert_before_symbol` for
targeted edits instead of rewriting a whole file. This matters especially around
`third_party/sdk`/`third_party/vcpkg` — large vendored trees where a naive `Read`/`grep` sweep is
expensive. Call `initial_instructions` at the start of a session if it hasn't been read yet (per
Serena's own MCP server instructions).

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

## Project status

Roadmap (bottom-up, see `MEMO.md` for the Japanese feature list and `docs/PROGRESS.md` for the
full per-phase implementation log): 0 SDK build → 1 file listing → 2 folder navigation
(double-click into subfolders) → 3 search → 4 download/open → 5 thumbnails → 6a categorized
logging + unified error-toast feedback → **6b file-list attribute display (size/modified date) +
sort (next)** → 6 local cache + open-folder background refresh (= MVP) → 7 realtime remote-change
reflection (post-MVP). Full bidirectional local sync is out of scope; upload has no assigned phase
yet (see `MEMO.md`'s feature list).

Phases 0–5 and 6a are done (6a was Phase 6's "エラーハンドリング全般" slice, pulled forward ahead
of the rest of Phase 6; 6b is the same kind of forward-pulled slice, not yet started — spec agreed
2026-07-28, see `MEMO.md`'s roadmap section for the decided details: Explorer-style detail list
view with sortable Name/Size/Modified columns, folders always sorted first, grid/thumbnail view
left unchanged). Core pieces in place: `IMegaClient`/`MegaSdkClient` (`src/core`/`src/mega`),
`FileListingService`/`FolderNavigationService`/`SearchService`/`DownloadService`/`ThumbnailService`
(`src/core`), their QML-facing controllers/`FileListModel` (`src/qml`), and cross-cutting app
infrastructure — categorized logging + a MEGA SDK logger bridge (`src/app`, `src/mega`) and a
shared `NotificationController`/`ErrorToast.qml` for user-facing failures (`src/qml`, `qml/`) — see
`docs/ARCHITECTURE.md` for the layering and `docs/PROGRESS.md` for what each phase actually built
and why.

## Build

Qt 6.10+, CMake 3.20+ (SDK's own minimum). **MSVC (VS2022) is the toolchain, not MinGW** — the
SDK's Windows build is only documented/supported for MSVC+vcpkg. **Must use the Visual Studio
generator, not Ninja**, regardless of IDE/kit — vendored SDK CMake hardcodes a VS-only toolset
variable (`docs/BUILD.md` has the exact mechanism). Qt is installed at both
`C:/Qt/6.11.1/mingw_64` (legacy, unused) and `C:/Qt/6.11.1/msvc2022_64` (current).

Submodules: `third_party/sdk` (pinned `v10.17.0`), `third_party/vcpkg` (**full history — do not
shallow-clone**, its baseline resolution needs `git show` on historical `versions/baseline.json`
blobs). Fresh clone: `git submodule update --init --recursive`, then
`third_party/vcpkg/bootstrap-vcpkg.bat`.

`CMakePresets.json`'s `msvc-debug` preset pins the full configure (generator, vcpkg toolchain,
`VCPKG_*` variables) — prefer it over hand-entering variables in Qt Creator (see `docs/BUILD.md`
for why). From the CLI: `cmake --preset msvc-debug` / `cmake --build --preset msvc-debug`. Manual
equivalent, which also documents each variable:

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

Build only `appMegaExplorer`, not the full solution — the SDK's `gfxworker` tool currently fails to
link, unrelated to our code (`docs/BUILD.md`). Test target: `MegaExplorerTests` instead, then run
via `ctest --preset msvc-debug` or `build/msvc-debug/tests/Debug/MegaExplorerTests.exe` directly.

Binary: `build/Desktop_Qt_6_11_1_MSVC2022_64bit_Debug/Debug/appMegaExplorer.exe`. Needs Qt's `bin`
and vcpkg's `debug/bin` on `PATH` to run outside Qt Creator:

```
set PATH=C:\Qt\6.11.1\msvc2022_64\bin;%CD%\build\Desktop_Qt_6_11_1_MSVC2022_64bit_Debug\vcpkg_installed\x64-windows-mega\debug\bin;%PATH%
```

**Compiler warnings**: `appMegaExplorer` builds at `/W4`. At the end of any task touching
`main.cpp`/`src/`, check for new warnings and fix them before considering the task done. Preferred:
the `qtcreator` MCP server (`mcp__qtcreator__build` / `list_issues` / `list_file_issues` — must be
running, see `docs/BUILD.md` for setup). CLI fallback:

```
C:/Qt/Tools/CMake_64/bin/cmake.exe --build build/msvc-debug --config Debug --target appMegaExplorer 2>&1 | grep -i "warning C" | grep -v "third_party"
```

Full path to `cmake.exe` is required — the `cmake` on `PATH` resolves to Strawberry Perl's copy,
which is unrelated and wrong for this project.

No linter or CI yet.

## Architecture

`main.cpp` is the composition root and QML bootstrap; `Main.qml` is the root `ApplicationWindow`
(new `.qml` files must be added to `CMakeLists.txt`'s `qt_add_qml_module`). `src/core`/`src/mega`
wrap the SDK's C++ API behind `IMegaClient`/`FileEntry` — any further MEGA-facing code must go
through that interface, never call `MegaApi`/`std::filesystem` directly. Manual constructor
injection against abstract interfaces (ports-and-adapters), no DI framework. Full directory
breakdown and the DI/testability design rationale: `docs/ARCHITECTURE.md`.

## Known environment issues

Git commands intermittently fail with a stale `.git/index.lock`. Recovery steps:
`docs/TROUBLESHOOTING.md` — follow them exactly (there's a safety check before removing the lock).
