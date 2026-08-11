# CLAUDE.md

Guidance for Claude Code when working in this repo. Kept compact — detail that isn't needed every
session lives in companion docs, linked from the relevant section below rather than inlined:

- `docs/REQUESTS.md` — **the inbox: where requests, new features and bugs get written, in free
  prose.** Each cycle folds it into `ROADMAP.md` and empties it, so nobody has to decide size or
  placement while writing. Labels (`[バグ]` `[最優先]` `[高]`/`[中]`/`[低]` `[削除]` `[編集]`) are
  optional; without `[最優先]` the loop only ever appends, so priority stays a human call.
- `docs/ROADMAP.md` — **the backlog, and the single source of truth for what gets built next.** The
  `/evolve` loop reads it every cycle and picks one item off the top. Sized S/M/L, and an **L is
  never implemented as-is** — a cycle that hits one only splits it. Its "見送り (blocked)" section
  is what the loop must not touch. Written by the loop, not by hand: keeping the human out of this
  file is what stops an edit racing a cycle's own removal of the row it just finished.
- `docs/roadmap-done.md` — one line per finished ROADMAP item, and the only log the loop writes to.
  Its メモ column is where "still needs checking on a real run" gets recorded.
- `docs/PROGRESS.md` — the phase-by-phase implementation log (what was built, why, gotchas), plus a
  Roadmap section that is now **history only**: the queue moved to `docs/ROADMAP.md`. A shipped
  phase's original plan sits at the top of its own log entry as a `> **Planned as.**` block
  (2026-08-07, to keep plan and outcome from drifting). Entries here are for phases a **human**
  cut — the loop never writes here. `README.md` is just a one-line title stub, not documentation.
- `docs/DESIGN_IMPROVEMENT.md` — the UI-tidying pass: measured findings, the D*/S* decision tables,
  and the per-stage log for S0–S11 (S0–S10 done, plus the unplanned S6a/S8a/S8b corrections). Visual work goes here, not in `docs/PROGRESS.md`;
  the four C++ changes it caused are cross-linked from both.
- `docs/REFACTOR_PLANS.md` — the pre-Phase-15 code-tidying pass (Japanese): the R1–R7 scope split,
  the per-item findings, and the log of what each fix decided. **All seven scopes are done
  (2026-08-08)**, so it is now a reference rather than a work queue — read it before cleanup work to
  see what was already decided, but new tidying items get their own document.
- `docs/ARCHITECTURE.md` — directory layout detail + the ports-and-adapters/DI design.
- `docs/BUILD.md` — rationale behind each build gotcha below (why VS generator, why
  `CMakePresets.json`, the FFmpeg link fix, etc.).
- `docs/investigations/` — one document per investigation (Japanese), kept after the work it fed
  lands. **There is no index file: the filename is the index**, so each is named `<KIND>_<TOPIC>.md`
  with `STUDY_` = feasibility study written before the phase it feeds, `SPEC_` = the spec for one
  screen's worth of behaviour, `BUG_` = a diagnosed defect. Every file states its own conclusion and
  status — which phase it fed, whether that shipped, what it left open — within its first ~10 lines,
  so `ls docs/investigations/` plus the head of one file answers most questions; `ast-outline` gets
  you the rest. Read the relevant `STUDY_`/`SPEC_` *before* planning that area; a `BUG_` is the
  record of a fix already made. New investigations go here, not in `docs/` directly, and take the
  same naming — no index entry to update.
- `docs/archive/` — retired documents, kept only so old commits and links still resolve. Nothing in
  here is current: every fact in it either moved to one of the docs above or went stale. Don't read
  it for answers and don't cite it; a document lands here instead of being deleted purely because
  the git history around it stays readable that way. `MEMO.md` was the first (2026-08-11 — its
  scope/feature list and licensing notes had been absorbed by this file and the Phase 20b log, and
  its stack table still claimed an app key and the SQLite cache that Phase 7b removed).

**Writing into those logs.** `docs/PROGRESS.md`, `docs/DESIGN_IMPROVEMENT.md` and
`docs/REFACTOR_PLANS.md` are append-only and already large, so a new entry gets **about one screen —
100 lines**. What earns the space is reasoning a later reader can't re-derive from the code: the
premise that turned out wrong, the approach tried and abandoned, the constraint that forced the
shape. Restating what the diff shows, or narrating a design that survived unchanged, doesn't. When
an item genuinely needs more room, it becomes its own `docs/investigations/` study and the entry
links to it. Each file states its own version of this at the top; follow that.

Check current file contents before assuming a feature exists — don't trust the roadmap alone.

## Tooling: use Serena on code, not `Read`/`Grep`/`Edit`

The Serena MCP server is running for this repo and is the **primary** way to read and edit
`src/`, `main.cpp`, and `tests/`. It indexes the tree symbolically, so it can hand back one
function body instead of the 900-line file around it — the same budget argument as `ast-outline`
below, applied to code. Call `initial_instructions` once per session before the first code task.

| Task | Tool |
| --- | --- |
| See a file's structure | `get_symbols_overview` |
| Read one symbol's body | `find_symbol` (`include_body=true`) |
| Find a symbol anywhere | `find_symbol` |
| Find callers / references | `find_referencing_symbols` |
| Declaration / implementations | `find_declaration` / `find_implementations` |
| Edit a symbol | `replace_symbol_body` |
| Add near a symbol | `insert_before_symbol` / `insert_after_symbol` |
| Pattern replace in a file | `replace_content` |
| Rename / delete a symbol | `rename_symbol` / `safe_delete_symbol` |

Normal edit flow: `get_symbols_overview` on the file → `find_symbol` with bodies for just the
symbols you'll touch → a symbolic edit. Don't read the whole file first "for context".

Built-in `Read`/`Grep`/`Glob`/`Edit` stay right for: non-code files (Markdown, JSON, YAML, CMake,
`.qml` is only partially indexed), a regex sweep across many files as a *discovery* step (follow up
with Serena on the hits), a few lines where a symbolic read is overkill, and any case where Serena
was tried on the target and failed. "The file is small" and "I already know the path" are not
reasons to skip it.

Serena's connection occasionally times out on session start; `/mcp` reconnects it. If it stays
down, say so rather than silently falling back to whole-file reads.

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
  patterns. Third-party notices are generated: after bumping any dependency, run
  `scripts/gen_third_party_notices.py` by hand to regenerate `licenses/` and
  `THIRD-PARTY-NOTICES.txt` (the reasoning behind each dual-license choice is in the Phase 20b log).

## Pre-release: refactoring existing code during planning is allowed

This project has not shipped yet, so there's no external API/data compatibility to preserve. When
planning a new feature, if an existing design constraint makes the implementation notably more
complex or risky, and reworking that existing code would avoid it, prefer proposing the rework
instead of working around the constraint — as long as it doesn't change product-level behavior/scope
in a significant way. If a plan includes such a rework, call it out explicitly and get the user's
go-ahead before proceeding (as part of normal plan review, not a separate approval step).

## Project status

Phases 0–15, 17–23a, 24a and 24b are done — several were pulled forward out of numeric order. The pre-15
code-tidying pass in `docs/REFACTOR_PLANS.md` finished on 2026-08-08 (all of R1–R7). **The numbered
phases are finished** — work now comes off `docs/ROADMAP.md` one item at a time, usually via the
`/evolve` loop (see "Loop engineering" below). A new numbered phase only appears if a human decides
some chunk is big enough to deserve one.

That one paragraph is deliberately all this file tracks. What a phase actually built, what it
changed its mind about mid-way, and what it knowingly left open is `docs/PROGRESS.md`: its Roadmap
section lists every phase with status, and `ast-outline outline docs/PROGRESS.md` gets you that list
without the 61k-token file behind it. Likewise the current architecture is `docs/ARCHITECTURE.md` —
there is no inventory of classes here to fall out of date.

Permanently out of scope: undo, full bidirectional local sync, and tearing a tab off into its own
window. Those three sit in `docs/ROADMAP.md`'s 見送り (blocked) section with their reasons, which is
where the loop reads them from.

## Loop engineering (`/evolve`)

Development now runs mostly as an unattended loop: `/loop 2h /evolve` fires
`.claude/skills/evolve/SKILL.md` every two hours, and each cycle takes **one** item off
`docs/ROADMAP.md`, implements it, verifies it, reviews it, and lands it as a single commit on a
fresh `evolve/NNN` branch. A cycle never touches `master`. Started 2026-08-11; the reasoning behind
each decision is in that skill file, not here.

**Which document may be written by whom** — this is the part that goes wrong if it isn't explicit:

| Document | Loop | Human |
| --- | --- | --- |
| `docs/REQUESTS.md` | reads it first every cycle, folds entries into ROADMAP, deletes them | **the inbox — free prose, this is where requests and bugs go** |
| `docs/ROADMAP.md` | takes items off, adds rows from the inbox, splits `L`s | rarely — the inbox is the front door |
| `docs/roadmap-done.md` | **one line per cycle — the only log it writes** | reads it; ticks off the 実機確認 column |
| `docs/PROGRESS.md` | never | 100-line entries, for human-cut phases only |
| `docs/DESIGN_IMPROVEMENT.md` | never (visual work needs eyes) | `/ui-style` sessions |
| `docs/investigations/` | reads; never creates | new STUDY / SPEC / BUG |
| `CLAUDE.md` | only when the user-facing feature set changed | anything |

**Taking a cycle's work into `master`** (human side, at the desk):

```
git log --oneline master..evolve/NNN     # one commit, read the subject
git diff master...evolve/NNN             # and the diff, since nothing else reviewed it with eyes
bash scripts/git_unlock.sh && git merge --ff-only evolve/NNN
```

If it turns out wrong after merging, `git revert` it and write the reason into `docs/ROADMAP.md` —
don't rewrite the branch, the loop computes its next number from the existing `evolve/NNN` names.

**Supporting pieces**, all of which exist for the loop but are useful by hand too:

- `scripts/loop_verify.sh` — the single verification entry point (see "Compiler warnings" below).
- `megatool` — a CLI over `IMegaClient` for setting up fixtures and for `whoami`. It logs in from
  `MEGAEXPLORER_TEST_ACCOUNT` / `MEGAEXPLORER_TEST_PASSWORD`, **independently of the app's saved
  session**, so it always sees the test account. Details: `docs/MEGATOOL.md`.
- **The app is signed in to a test MEGA account, and stays that way.** The loop launches the app,
  which auto-logs-in from the saved session, so a production session would put real files in front
  of destructive checks — and in `.screenshots/`. Every cycle aborts unless `megatool whoami`
  matches `MEGAEXPLORER_TEST_ACCOUNT`.
- `scripts/drive_gate.cmd` + `scripts/drive_gate_hook.sh` — `ui_shot.py drive` hijacks the real
  mouse and keyboard, so a `PreToolUse` hook denies it unless an **expiring** flag file under
  `%LOCALAPPDATA%\MegaExplorerLoop\` says otherwise. The desktop shortcuts (`drive ON 6h/8h/12h`,
  `drive OFF`) write it. Default is off, and a denial is a skip, not a failure. Don't lock the
  workstation while it's on: `SendInput` goes to the secure desktop and never reaches the app.
- `scripts/ntfy-send.sh` — how the loop reaches a phone. Claude Code's built-in push reports success
  and mostly doesn't deliver, so it isn't used.

Secrets: `MEGAEXPLORER_TEST_ACCOUNT` and `NTFY_TOPIC` live in the gitignored
`.claude/settings.local.json` `env` block; `MEGAEXPLORER_TEST_PASSWORD` is a Windows user
environment variable, set by hand, never in any file. A new value there needs a fresh Claude Code
session before it is visible.

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
for why). **Always invoke CMake as `C:/Qt/Tools/CMake_64/bin/cmake.exe`, never bare `cmake`** — the
`cmake` on `PATH` is Strawberry Perl's 3.29, which is too old to know this MSVC and fails configure
with `target_compile_features no known features for CXX compiler "MSVC"`, *after* overwriting
`CMakeCache.txt`; re-run the preset with the full path to recover. `CMAKE_COMMAND:INTERNAL` in that
cache tells you which copy last configured the tree. So, from the CLI:

```
C:/Qt/Tools/CMake_64/bin/cmake.exe --preset msvc-debug
C:/Qt/Tools/CMake_64/bin/cmake.exe --build --preset msvc-debug
```

Manual equivalent of the configure, which also documents each variable (same full path):

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
link, unrelated to our code (`docs/BUILD.md`). Test targets: `MegaExplorerTests` (GoogleTest) and
`MegaExplorerQmlTests` (Qt Quick Test over `qml/`, sources in `tests/qml/`); `ctest --preset
msvc-debug` runs both. The binaries are `build/msvc-debug/Debug/MegaExplorer{,Qml}Tests.exe`.
Running the QML one by hand needs `-o -,tap`: QtTest's default logger sends its output to
`OutputDebugString` instead of stdout whenever no console is attached to the process, which is
every run from Git Bash, so without it a failure prints nothing at all.

Binary: `build/msvc-debug/Debug/appMegaExplorer.exe`. Needs Qt's `bin`
and vcpkg's `debug/bin` on `PATH` to run outside Qt Creator:

```
set PATH=C:\Qt\6.11.1\msvc2022_64\bin;%CD%\build\msvc-debug\vcpkg_installed\x64-windows-mega\debug\bin;%PATH%
```

**Compiler warnings**: all six of our targets — `MegaExplorerCore`, `MegaExplorerQml`,
`appMegaExplorer`, `megatool`, `MegaExplorerTests`, `MegaExplorerQmlTests` — build at `/W4`, via the
`MegaExplorerWarnings` interface
target they each link (`docs/BUILD.md` covers that and the two Qt-header suppressions it carries).
At the end of any task touching
`main.cpp`/`src/`, check for new warnings and fix them before considering the task done. The one
command that does it — and builds and tests as well — is:

```
bash scripts/loop_verify.sh          # add --full to see the generated sources' warnings
```

It closes a running `appMegaExplorer.exe` first (otherwise the link dies with `LNK1104`),
reconfigures when `QML_FILES` changed, **fails on a single warning of ours**, then runs `ctest`, and
prints only a handful of lines unless something failed. It is what `/evolve` uses, so it must not
depend on an IDE. The `qtcreator` MCP server (`mcp__qtcreator__build` / `list_issues` /
`list_file_issues`, see `docs/BUILD.md`) is an optional convenience for interactive sessions only.

After adding/removing a `.qml` file from `qt_add_qml_module`'s `QML_FILES`, **or moving the module
to a different target**, **reconfigure** (`--preset msvc-debug`, full path as above) before
rebuilding — an
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

## Code comments: only what the code can't say

A comment earns its place only if it carries something a later reader **cannot** recover from the
code around it. Three kinds qualify, and each gets **one or two lines**, not a paragraph:

- **External-spec traps** — behaviour of Qt/MSVC/Win32/the SDK that forced the code's shape (MSVC's
  `_IOLBF` is an alias for full buffering; `AppDataLocation` is the *roaming* path on Windows).
- **Guards against a plausible "fix"** — the edit someone would reasonably make that would break it
  (don't gate the ANSI codes on `_isatty`: the target consumer is a pipe, not a tty).
- **Off-screen forces** — why a thread/lifetime/ownership choice exists, when the cause lives in
  another file (this mutex exists because SDK callbacks arrive on an SDK-internal thread).

Everything else goes. Specifically: restating what the code does, verification narrative ("confirmed
against Qt's docs"), scope justifications ("no size cap — this is a one-user desktop app"), long
trade-off footnotes about paths not taken, change history (that's the commit's job), and the same
fact said three ways. One fact, one line. Prose that needs more room belongs in `docs/`, and the
comment can link there.

This is also a **cleanup rule**, not just a writing rule: when you touch a file whose comments
predate this section, prune them by the same test in the same edit. R7 in `docs/REFACTOR_PLANS.md`
already swept `src/` and `main.cpp` once under this rule (3049 → 1832 comment lines);
`src/app/Logging.cpp` is the worked example there — 45 comment lines to 20. `tests/` was left out of
that sweep, so it is the one tree where pre-rule comments are still expected.

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
