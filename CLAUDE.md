# CLAUDE.md

Guidance for Claude Code when working in this repo. Kept compact — detail that isn't needed every
session lives in companion docs, linked from the relevant section below rather than inlined:

- `docs/MEMO.md` — non-roadmap project notes: scope, tech stack, feature list, licensing, open
  technical concerns (Japanese). `README.md` is just a one-line title stub, not documentation.
- `docs/PROGRESS.md` — the roadmap (what's next and why-in-this-order) plus the full phase-by-phase
  implementation log (what was built, why, gotchas). Single source of truth for both, now that
  `docs/MEMO.md`'s former roadmap section moved here (2026-07-28, to end dual-tracking drift between
  the two files).
- `docs/DESIGN_IMPROVEMENT.md` — the UI-tidying pass: measured findings, the D*/S* decision tables,
  and the per-stage log for S0–S11 (S0–S10 done, plus the unplanned S6a/S8a/S8b corrections). Visual work goes here, not in `docs/PROGRESS.md`;
  the four C++ changes it caused are cross-linked from both.
- `docs/REFACTOR_PLANS.md` — the pre-Phase-15 code-tidying pass (Japanese): the R1–R6 scope split,
  the per-item findings, and the log of what each fix decided. Active work stream; check it before
  starting anything that looks like cleanup.
- `docs/ARCHITECTURE.md` — directory layout detail + the ports-and-adapters/DI design.
- `docs/BUILD.md` — rationale behind each build gotcha below (why VS generator, why
  `CMakePresets.json`, the FFmpeg link fix, etc.).
- `docs/*_INVESTIGATION.md` — standing feasibility studies, written before the phase they feed and
  kept afterwards (Japanese). Read the relevant one *before* planning that phase; each states its
  own conclusion up front, so the outline + first section is usually enough:
  - `TITLEBAR_TABS_INVESTIGATION.md` — fed Phase 17 (frameless window + caption-row tabs).
  - `FETCHNODES_PROGRESS_INVESTIGATION.md` — fed Phase 18; carries the measured 600k-node timings
    and the reason the decrypt phase can't show progress.
  - `CROSS_TAB_DND_INVESTIGATION.md` — spring-loaded tabs, i.e. the drop-onto-tab half of
    Phase 22b, which shipped. Its one open risk did not materialize; see the Phase 22b log.
  - `CROSS_PLATFORM_INVESTIGATION.md` — whether the MSVC/vcpkg-only build could go
    Linux/macOS. No phase attached; conclusion is that `WindowsSessionStore` (DPAPI) is the real
    work, not the build files.

Check current file contents before assuming a feature exists — don't trust the roadmap alone.

## Tooling: use `ast-outline` on the docs, not `Read`

The companion docs above are long — `docs/DESIGN_IMPROVEMENT.md` alone is ~15k tokens, and
`docs/PROGRESS.md` is comparable. Reading one whole just to answer a question about one stage
burns most of a session's budget on text that isn't needed. Use `ast-outline` (a CLI on `PATH`,
`~/.local/bin`) instead: it parses Markdown structurally — headings become symbols, with line
ranges — so a section can be pulled out without the file around it. Same tool works on source
files, but for code Serena's symbolic tools are still the better fit; this section is about the
docs.

The three subcommands, in the order they're normally used:

```
ast-outline outline docs/DESIGN_IMPROVEMENT.md      # heading tree + line ranges + token estimate
ast-outline grep 'S7' docs/DESIGN_IMPROVEMENT.md    # hits, each annotated with its heading scope
ast-outline show docs/DESIGN_IMPROVEMENT.md '2-1. 行全体に左右マージンがない' '2-4. 検索欄'
```

Tips:

- `show` matches heading text **exactly**, so run `outline` or `grep` first to get the real string —
  don't guess it from memory.
- `grep` is the one to reach for when a topic is scattered (an S* stage typically appears in the
  checklist, the decision table, the implementation order, and the acceptance criteria). Its
  heading-scope annotation tells you which hits are worth a `show`.
- `show` takes several headings in one call — batch them rather than calling it per section.
- Falling back to `Read` is fine when the answer really does need the whole file, or for short docs
  where the outline round-trip costs more than it saves.

## What this project is

Windows-Explorer-like desktop client for MEGA cloud storage — thumbnail grid, search,
double-click-to-open (gaps in official MEGAsync). Not a sync client: only a one-shot background
refresh when a folder is opened, no continuous watching.

- No MEGA app key required (dropped 2023) — pass `nullptr` to `MegaApi`'s constructor
  (`meganz/sdk` issue #2706).
- Licensing: app is **MIT** (relicensed from GPLv3 on 2026-08-07). Qt is used under **LGPLv3**, so
  adding a GPL-only Qt module (Charts, Virtual Keyboard, …) would break that — check before
  importing one. LibRaw is statically linked under the LGPL, which the published source satisfies;
  the app cannot go closed-source. MEGA SDK is BSD-2-Clause. `meganz/MEGAsync`'s own source is
  under a restrictive Code Review Licence — never copy code from it, reference only for SDK usage
  patterns.
- Stack detail (thumbnail/cache libraries etc.) is in `docs/MEMO.md`, not repeated here.

## Pre-release: refactoring existing code during planning is allowed

This project has not shipped yet, so there's no external API/data compatibility to preserve. When
planning a new feature, if an existing design constraint makes the implementation notably more
complex or risky, and reworking that existing code would avoid it, prefer proposing the rework
instead of working around the constraint — as long as it doesn't change product-level behavior/scope
in a significant way. If a plan includes such a rework, call it out explicitly and get the user's
go-ahead before proceeding (as part of normal plan review, not a separate approval step).

## Project status

Phases 0–14b and 17–23a are done — several were pulled forward out of numeric order. **Next up:
phases 15–16** (in-app preview, real-time remote-change reflection), with a pre-15 code-tidying pass
running first in `docs/REFACTOR_PLANS.md`.

That one paragraph is deliberately all this file tracks. What a phase actually built, what it
changed its mind about mid-way, and what it knowingly left open is `docs/PROGRESS.md`: its Roadmap
section lists every phase with status, and `ast-outline outline docs/PROGRESS.md` gets you that list
without the 61k-token file behind it. Likewise the current architecture is `docs/ARCHITECTURE.md` —
there is no inventory of classes here to fall out of date.

Permanently out of scope: undo, full bidirectional local sync, and tearing a tab off into its own
window.

## Build

Qt 6.10+, CMake 3.20+ (SDK's own minimum). **MSVC (VS2022) is the toolchain, not MinGW** — the
SDK's Windows build is only documented/supported for MSVC+vcpkg. **Must use the Visual Studio
generator, not Ninja**, regardless of IDE/kit — vendored SDK CMake hardcodes a VS-only toolset
variable (`docs/BUILD.md` has the exact mechanism). Qt is installed at both
`C:/Qt/6.11.1/mingw_64` (legacy, unused) and `C:/Qt/6.11.1/msvc2022_64` (current).

Submodules: `third_party/sdk` (pinned `v10.17.0`), `third_party/vcpkg` (**full history — do not
shallow-clone**, its baseline resolution needs `git show` on historical `versions/baseline.json`
blobs), `third_party/qwindowkit` (pinned tag `1.5.0`; has its own nested `qmsetup`/`syscmdline`
submodules, so `--recursive` is mandatory, not just tidy — see `docs/BUILD.md`). Fresh clone:
`git submodule update --init --recursive`, then `third_party/vcpkg/bootstrap-vcpkg.bat`.

`CMakePresets.json`'s `msvc-debug` preset pins the full configure (generator, vcpkg toolchain,
`VCPKG_*` variables) — prefer it over hand-entering variables in Qt Creator (see `docs/BUILD.md`
for why). From the CLI: `cmake --preset msvc-debug` / `cmake --build --preset msvc-debug`. Manual
equivalent, which also documents each variable:

```
cmake -S . -B build/msvc-debug -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64 -DCMAKE_BUILD_TYPE=Debug ^
    -DVCPKG_ROOT=third_party/vcpkg ^
    -DCMAKE_TOOLCHAIN_FILE=third_party/vcpkg/scripts/buildsystems/vcpkg.cmake ^
    -DVCPKG_MANIFEST_DIR=third_party/sdk ^
    -DVCPKG_TARGET_TRIPLET=x64-windows-mega ^
    -DVCPKG_OVERLAY_PORTS=third_party/sdk/cmake/vcpkg_overlay_ports ^
    -DVCPKG_OVERLAY_TRIPLETS=third_party/sdk/cmake/vcpkg_overlay_triplets ^
    -DVCPKG_MANIFEST_FEATURES="use-openssl;use-freeimage;use-ffmpeg;use-pdfium;sdk-tests"
cmake --build build/msvc-debug --config Debug --target appMegaExplorer
```

Build only `appMegaExplorer`, not the full solution — the SDK's `gfxworker` tool currently fails to
link, unrelated to our code (`docs/BUILD.md`). Test target: `MegaExplorerTests` instead, then run
via `ctest --preset msvc-debug` or `build/msvc-debug/tests/Debug/MegaExplorerTests.exe` directly.

Binary: `build/msvc-debug/Debug/appMegaExplorer.exe`. Needs Qt's `bin`
and vcpkg's `debug/bin` on `PATH` to run outside Qt Creator:

```
set PATH=C:\Qt\6.11.1\msvc2022_64\bin;%CD%\build\msvc-debug\vcpkg_installed\x64-windows-mega\debug\bin;%PATH%
```

**Compiler warnings**: all four of our targets — `MegaExplorerCore`, `MegaExplorerQml`,
`appMegaExplorer`, `MegaExplorerTests` — build at `/W4`, via the `MegaExplorerWarnings` interface
target they each link (`docs/BUILD.md` covers that and the two Qt-header suppressions it carries).
At the end of any task touching
`main.cpp`/`src/`, check for new warnings and fix them before considering the task done. Preferred:
the `qtcreator` MCP server (`mcp__qtcreator__build` / `list_issues` / `list_file_issues` — must be
running, see `docs/BUILD.md` for setup). CLI fallback:

```
C:/Qt/Tools/CMake_64/bin/cmake.exe --build build/msvc-debug --config Debug --target appMegaExplorer 2>&1 | grep -i "warning C" | grep -v "third_party"
```

Full path to `cmake.exe` is required — the `cmake` on `PATH` resolves to Strawberry Perl's copy,
which is unrelated and wrong for this project.

After adding/removing a `.qml` file from `qt_add_qml_module`'s `QML_FILES`, **or moving the module
to a different target**, **reconfigure** (`cmake --preset msvc-debug`) before rebuilding — an
incremental `cmake --build` alone leaves the AOT `qmlcache_loader.cpp` aggregator stale and the
link fails with unresolved `QmlCacheGeneratedCode::...` symbols. The aggregator's symbol names
embed the target name, which is why a target move triggers it too (`QML_FILES` case hit during
Phase 9, see its `docs/PROGRESS.md` log entry; target case confirmed in R4-4).

Warnings from the generated sources (moc, qmlcachegen, type registration) only appear on a **full**
rebuild, since an incremental build has no reason to recompile them — don't read a clean
incremental build as proof the tree is warning-free.

No linter or CI yet.

## Style tuning

Visual/QML tweaking runs through the `ui-style` skill (`.claude/skills/ui-style/`) — invoked
automatically when the task is about looks, or explicitly as `/ui-style`. Its
`scripts/ui_shot.py` wraps the whole loop: `cycle <name>` does close → build → launch →
screenshot in one command and drops a PNG in `.screenshots/` (gitignored), and `launch --size WxH`
opens the window at a given size. Options and failure modes: the skill's `reference.md`. The
`drive` subcommand (click/keystroke injection) hijacks the real mouse — **ask the user before
running it**.

Light/dark is checked without touching the Windows theme setting: the env var
`MEGAEXPLORER_COLOR_SCHEME=light|dark` (read in `main.cpp`, applied via
`QStyleHints::setColorScheme()`), or `launch`/`cycle`'s `--theme light|dark|system` which sets it
for you.

## Architecture

`main.cpp` is the composition root and QML bootstrap; `Main.qml` is the root `ApplicationWindow`
(new `.qml` files must be added to `CMakeLists.txt`'s `qt_add_qml_module`). `src/core`/`src/mega`
wrap the SDK's C++ API behind `IMegaClient`/`FileEntry` — any further MEGA-facing code must go
through that interface, never call `MegaApi`/`std::filesystem` directly. Manual constructor
injection against abstract interfaces (ports-and-adapters), no DI framework. Full directory
breakdown and the DI/testability design rationale: `docs/ARCHITECTURE.md`.

## Git: every write goes through `scripts/git_unlock.sh`

This repo intermittently leaves a stale `.git/index.lock` behind, so a later `git add`/`commit`/
`checkout` fails with `Unable to create '.git/index.lock': File exists` while nothing is actually
running. That is normal here, not an incident. **Chain the unlock script ahead of every git write**,
so a non-zero exit stops the write instead of you reacting to a failure afterwards:

```
bash scripts/git_unlock.sh && git commit -F - <<'EOF'
...
EOF
```

The script prints one line and removes the lock only when nothing can plausibly be holding it (no
`git.exe` running, lock at least 10s old), so running it unconditionally is the intended use — don't
inspect anything by hand first. **Never `rm` the lock yourself**: doing that under a live git process
corrupts the index, which is the whole reason those two checks exist.

| Output | Exit | Meaning |
| --- | --- | --- |
| `no lock` | 0 | Nothing to do; the git command runs. |
| `removed stale lock (Ns old)` | 0 | Cleared; the git command runs. |
| `git.exe is running -- lock left alone` | 1 | A real operation is live. Wait, re-run. |
| `lock is Ns old -- too fresh to call stale` | 1 | Same: wait, re-run. |

Windows/Git Bash only. Rationale for each check is in the script's own header comment.
