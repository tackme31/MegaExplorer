# Project Progress Log

Roadmap (what's next, why in this order) + phase-by-phase implementation log (what was built, key
decisions/gotchas). Single source of truth for the roadmap; `docs/MEMO.md` keeps only non-roadmap
notes; `CLAUDE.md` has a condensed current-status summary pointing here.

## Roadmap

Bottom-up: each phase independently verifiable before the next starts. MVP = phases 0–6; phases 7+
are post-MVP, sequenced by priority/dependency.

| # | Phase | Status |
|---|---|---|
| 0 | SDK build + CLI login | done |
| 1 | Bare file listing (root only) | done |
| 2 | Folder navigation | done |
| 3 | Search (MVP scope) | done |
| 4 | Download → open | done |
| 5 | Thumbnails + grid view | done |
| 6a | Categorized logging + error-toast feedback | done (pulled forward) |
| 6b | File-list attribute display + sort | done (pulled forward) |
| 6 | Local cache + open-folder background refresh | done (closes out MVP) |
| 7a | Session storage foundation (`ISessionStore`/`WindowsSessionStore`) | done |
| 7 | Login screen + session persistence (remaining wiring) | planned |
| 8 | Breadcrumb trail | planned |
| 9 | Windows-Explorer-style tabs (multiple folder views) | planned |
| 10 | Folder tree navigation (side panel) | planned |
| 11 | Quick access (pinned folders, side panel) | planned |
| 12 | Rename / delete (move to rubbish) / move | planned |
| 13 | Multi-select + bulk operations | planned |
| 14 | Upload (drag & drop) | planned |
| 15 | In-app preview (side panel, `getPreview`) | planned |
| 16 | Real-time remote-change reflection | future, post-MVP |
| 17+ | Undecided | full bidirectional local sync stays out of scope |

### Phase 6 — local cache + open-folder background refresh

Persist the node tree locally (SQLite) and run a one-shot background refresh on folder open — not
continuous watching. See the Phase 6 implementation-log entry below for what was built.

### Phase 7a — session storage foundation

Standalone slice, built and tested ahead of the rest of Phase 7 so it could be validated in
isolation before anything depends on it. See the Phase 7a implementation-log entry below for what
was built.

### Phase 7 — login screen + session persistence

`main.cpp` currently requires `MEGA_EMAIL`/`MEGA_PWD` env vars, no login UI. `ISessionStore`/
`WindowsSessionStore` (Phase 7a) are ready; remaining scope: `IMegaClient::loginWithSession` (or
equivalent) wiring `dumpSession()`/`fastLogin()` through `MegaSdkClient`, `main.cpp`
composition-root changes (constructing `WindowsSessionStore` with a real `session.dat` path), the
login screen itself (QML) and removing the env-var requirement, and logout/"forget session" UI.

### Phase 8 — breadcrumb trail

Known gap since Phase 2. Cheap: `FolderNavigationService` already keeps a back-stack of
`Location`s, so mostly exposing existing state to QML.

### Phase 9 — Windows-Explorer-style tabs (multiple folder views)

Tab strip above the file view, each tab its own independent navigation context (back-stack, current
folder, search state) rather than one shared `FolderNavigationService` instance. Placed right after
breadcrumb (phase 8) and before the folder-tree side panel (phase 10) and quick access (phase 11)
deliberately: those two become shared chrome sitting beside N tabbed content panes, cheaper to design
that way from the start than retrofitting tab-awareness onto a side panel that was built assuming a
single pane. Likely needs `FolderNavigationService`/`FolderNavigationController` instantiated per tab
(or refactored to take a tab id) rather than the current single composition-root instance;
`SearchService`'s scope-via-`currentLocation()` convention (phase 3) needs the same per-tab
treatment. New tab defaults to root; closing the last tab closes the window (matching Explorer).
Persisting the open tab set across restarts is a stretch goal, not required for the phase to be done.

### Phase 10 — folder tree navigation (side panel)

New left side panel; grouped with breadcrumb as "where am I / where can I go" nav, and becomes the
home for quick access (phase 11). Expected to reuse `IMegaClient::getChildren` for lazy expansion;
check performance on large folders. Shared across tabs (phase 9), not duplicated per tab.

### Phase 11 — quick access (pinned folders, side panel)

Adds a pinned-folders section to phase 10's panel. Persist via `Settings` (QtCore). Dangling-pin
handling (target deleted/moved) should be designed together with phase 12, not bolted on after.

### Phase 12 — rename / delete (move to rubbish) / move

Each maps to a single SDK request (`rename`/`moveToRubbish`/`moveNode`), no transfer-listener
needed. Prerequisite for phase 13. Mainly `FileContextMenu.qml` + matching `IMegaClient` methods.

### Phase 13 — multi-select + bulk operations

Sequenced after phase 12 (bulk ops need single-item versions first). Needs a selection model for
both `TableView` and `GridView`. Bulk download can likely reuse `DownloadService`'s existing queue.

### Phase 14 — upload (drag & drop)

Symmetric to phase 4's download but needs its own `MegaApi::startUpload` transfer listener
(mirroring `DownloadService`), higher cost than phase 12. Drag & drop via Qt Quick's `DropArea`.

### Phase 15 — in-app preview (side panel, `getPreview`/`startStreaming`)

Lowest priority; explicitly deferred in Phase 4 since download→open already covers "view the file".
Format-specific rendering (image/PDF/text/...) makes this the highest-effort item on the list.

### Phase 16 (future) — real-time remote-change reflection

Reflect other devices' changes via the SDK's push-notification mechanism into whatever listing is
open. Additive on top of phase 6's refresh.

### Phase 17+ — undecided

Full bidirectional local sync stays out of scope for the foreseeable future.

## Phase 0 — SDK build & CLI login (done)

MEGA SDK v10.17.0 + vcpkg vendored under `third_party/`; `appMegaExplorer` links `MEGA::SDKlib`;
CLI login/fetchNodes verified.

## Phase 1 — bare file listing (done)

`IMegaClient`/`MegaSdkClient` (`src/core`/`src/mega`) + `FileListingService` (login→fetchNodes→
getRootChildren) + `FileListModel` (`src/qml`). Tested via `MockMegaClient`.

## Phase 2 — folder navigation (done)

`FolderNavigationService` adds `openFolder`/`goBack`, back-stack-based, root modeled as a sentinel
`Location` (no real handle). `FolderNavigationController` wraps it + `FileListingService`, is the
`controller` context property. Double-click-to-open, header Back button gated on `canGoBack`.

## Phase 3 — search (done)

MVP scope: search box in header, recursive search scoped to the currently open folder
(`MegaSearchFilter::byName` + `byLocationHandle`, via synchronous `MegaApi::search()`).
`IMegaClient::search` mirrors the root-sentinel `Location` convention. `SearchService` resolves
scope via `FolderNavigationService::currentLocation()`. Submits on Enter only (call blocks the GUI
thread). Filter button for account-wide search/pagination deferred, not built.

## Phase 4 — download → open (done)

Double-click and right-click "Download" both call `DownloadController::downloadFile`.
`IMegaClient::download` takes a progress callback + `Result<DownloadOutcome>` completion callback
(`MegaTransferListener`-based, unlike the rest of the interface). `alreadyPresent` (identical file
already at destination, SDK skipped the transfer) is inferred from
`transferredBytes()==0 && totalBytes()>0` at completion — no public SDK getter for this. On a
same-name-different-content collision the SDK renames the new file (`(1)`, `(2)`, ...) rather than
overwriting; the snackbar reports the actual saved leaf name, not the requested one.
`DownloadService` serializes downloads one at a time, auto-advancing the queue. **Gotcha**:
destination paths must be `QDir::toNativeSeparators()`d before `MegaApi::startDownload` — the SDK's
`LocalPath` splits on `\` on Windows, so a `/`-path gets treated as one giant leaf name and trips an
internal assert. Files save to the real Downloads folder, no app subfolder. In-app preview and
upload out of scope (deferred to phases 14/13).

## Phase 5 — thumbnails (done)

Server-generated thumbnails only (`MegaNode::hasThumbnail()` + `getThumbnail()`); no local
generation for files without one (extension-based fallback icon). In-memory-only cache (persistence
is phase 6). List/grid toggle persisted via `Settings` (QtCore).

`ThumbnailService` sits in front of `IMegaClient::getThumbnail` with: a handle→path success cache,
in-flight de-dup, and bounded parallel execution (cap 4, vs. `DownloadService`'s serial queue).
Failures aren't cached. `FileListModel` updates thumbnail rows via per-row `dataChanged()` (not
`beginResetModel()`) to avoid flicker while scrolling.

**Gotchas**: an inline QML `component` can't be a sibling before the root object in a `.qml` file —
`qmlcachegen` errors; must be a direct child inside the root. Grid delegate's `Image.source` needs
`thumbnailPath.replace(/\\/g, "/")` before `"file:///"` prefix — literal backslashes break URL
parsing. `Settings`/`QtCore` QML module needs no extra `CMakeLists.txt` entry (ships with
`Qt6::Qml`).

## Phase 6a — categorized logging + unified error-toast feedback (done)

Motivation: several failure paths only did a bare `qWarning()`, and since `appMegaExplorer` is
`WIN32_EXECUTABLE`, that output reached nowhere visible on a normal launch. Fixed in passing: a
`DownloadJob` failure branch dropped `errorCode`.

`src/app/Logging.{h,cpp}`: `QLoggingCategory` per area (`lcApp`/`lcNavigation`/`lcSearch`/
`lcDownload`/`lcThumbnail`/`lcSdk`) + `qInstallMessageHandler` writing to stderr and
`MegaExplorer.log` under `QStandardPaths::AppLocalDataLocation` (not `AppDataLocation` — that's the
*roaming* profile on Windows, would sync a growing log file over the network). Single-generation
rotation (`.log` → `.log.1` on startup). Must run before any other logging call (installed in
`main.cpp` right after `setOrganizationName`/`setApplicationName`). Kept out of `MegaExplorerCore`
to preserve its Qt-free-ness.

`MegaSdkLogger` (`src/mega`, needs `megaapi.h`) bridges `mega::MegaLogger` into `lcSdk`, registered
via `MegaApi::addLoggerObject` in `MegaSdkClient`'s ctor/dtor.

`NotificationController` (`src/qml`): one `errorOccurred(context, message)` signal; QML
(`ErrorToast.qml`, mirrors `DownloadSnackbar.qml`'s visual style) composes the localized text per
context. Threaded via constructor injection into `FolderNavigationController`/
`ThumbnailController`/`DownloadController`. `DownloadController`'s actual download-failure path
logs but deliberately doesn't call `notifyError()` — `DownloadSnackbar` already surfaces it.

## Phase 6b — file-list attribute display + sort (done)

List view changed from single-column `ListView` to an Explorer-style detail view: sortable Name /
Date modified / Size columns, folders always first (SDK's own guaranteed ordering, no client-side
logic needed). A "Kind" column was considered and dropped (would need client-side extension
classification, conflicting with server-side sort). Grid/thumbnail view (Phase 5) untouched.

Sorting is server-side (`MegaApi::getChildren`/`search`'s `order` arg) to hold up on huge folders.
`src/core/SortOrder.h` (`SortKey::{Name,Size,ModificationTime}` + `ascending`) threaded through
`IMegaClient`/`MegaSdkClient`. `FolderNavigationController::setSortOrder` re-fetches whichever
listing (folder or search) is currently shown via `FolderNavigationService::refreshCurrent`.

Rendering moved to `TableView`+`HorizontalHeaderView`; `FileListModel` rewritten as a
`QAbstractTableModel` (3 columns). Column headers are hardcoded in `FileTableView.qml`, not from
`headerData()`.

**Gotchas** (`FluentWinUI3` style): `HorizontalHeaderView` ships no default delegate — its
`Rectangle` needs explicit `color: "transparent"` or the header text renders white-on-white.
`textRole` must be set to `""` (defaults to `"display"`, which the model doesn't provide) to avoid a
role-warning spam. Both `TableView`/`HorizontalHeaderView` default to click-drag panning — fixed
with `acceptedButtons: Qt.NoButton` (Qt 6.9+), wheel scroll unaffected.

Column widths + sort column/direction persist via `Settings`. Widths must be reapplied on
`TableView.onRowsChanged` too (not just `Component.onCompleted`) since `FileListModel::setEntries()`
does a full model reset on every navigation. `saveColumnWidths()` hooks `TableView.onLayoutChanged`
(only official resize hook, also fires on scroll — harmless, same-value assignment is a no-op).

Date modified matches Windows Explorer's format (4-digit year), not reproducible via
`Qt.formatDateTime(date, Locale.ShortFormat)` (2-digit year for en_US) — hand-written format string
`"M/d/yyyy h:mm AP"` (`AP` = Qt's AM/PM token; don't confuse with `tt`, which is timezone in Qt).
Folders show a blank Date modified cell rather than the Unix epoch (`getModificationTime()` returns
0 for folders).

All list/table UI strings landed in English, consistent with the rest of the shipped UI — no
localization infrastructure yet.

## Phase 6 — local cache + open-folder background refresh (done)

Closes out the MVP. Persists the MEGA node tree locally in SQLite (`node_cache.sqlite3` under
`QStandardPaths::AppLocalDataLocation`, same non-roaming rationale as Phase 6a's log file) and shows
a cached listing immediately on folder open, replacing it with a one-shot authoritative SDK fetch
shortly after — not continuous sync/watching (deferred to Phase 15).

New port `INodeCache` (`src/core/INodeCache.h`) rather than the generic `IFileSystem`/
`std::filesystem` wrapper `docs/ARCHITECTURE.md` had speculatively pre-declared for this phase: the
actual need turned out to be a structured node-tree cache, and MEGA SDK's own vcpkg manifest already
builds SQLite (`sqlite3`, importable as `unofficial-sqlite3` → `unofficial::sqlite3::sqlite3`; it's
linked `PRIVATE` into `SDKlib` so the app has to `find_package` it independently, same fix shape as
the existing FFmpeg::swscale workaround). `INodeCache` is synchronous (`Result<T>` returned
directly), deliberately not callback-shaped like `IMegaClient` — it has no genuinely-async sibling
to stay interface-consistent with, so a callback shape would only add indirection. Adapter
`SqliteNodeCache` (`src/platform/SqliteNodeCache.h`/`.cpp`, new `src/platform/` directory) is the
only file allowed to include `sqlite3.h`, using the raw C API directly (prepared statements, no
ORM/wrapper library). Not part of `MegaExplorerCore` (parallels `src/mega`), but — unlike
`MegaSdkClient`, which needs a live account — it's fully offline-testable, so it gets its own real
adapter test (`tests/SqliteNodeCacheTest.cpp`) against a `:memory:` database in addition to the
`MockNodeCache`-based tests elsewhere.

Schema is one table, `node_cache`, keyed by a composite primary key `(parent_is_root, parent_handle,
handle)` — the `isRoot` sentinel is threaded through everywhere (same convention as
`FolderNavigationService::Location`) so a folder with handle 0 and "the root" never collide.
`saveChildren` is a full delete-then-insert per parent, transactional — no incremental diffing.
`loadChildren` never distinguishes "never cached" from "cached as empty" (both return `Result::ok`
with an empty vector); that "empty means nothing to show yet" policy lives one layer up, in
`FolderNavigationService`, not in the store itself.

`FolderNavigationService::openFolder`/`goBack` (and a new `openRoot`, replacing `FileListingService`
previously calling `IMegaClient::getRootChildren` directly) each take two callbacks instead of one:
`onCacheHit` (fires synchronously, at most once, only for a non-empty cache hit) and `onRefreshed`
(always fires exactly once, with the authoritative network result, which on success is also written
back to the cache). A shared private `loadWithCache` helper holds the "look up cache → make the
network call → commit navigation state → write back → onRefreshed" sequence so it isn't duplicated
three times. `refreshCurrent` (sort-order changes on an already-open folder) deliberately keeps its
original single-callback shape — nothing useful for a cache read to add when something's already on
screen — but still writes its result through to the cache. `FileListingService::loadRootListing`
now delegates its final step to `FolderNavigationService::openRoot` instead of calling
`IMegaClient::getRootChildren` itself, so the root listing (the very first thing shown after the
slowest part of startup) gets the same cache-then-refresh treatment as any other folder;
`FileListingService` gained a `FolderNavigationService` constructor dependency for this (same
already-established pattern `SearchService` uses, not a new one). `main.cpp`'s composition root
reorders construction so `navigationService` exists before `listingService`.

`FolderNavigationController` (`src/qml`) gained a thinner `applyCacheHit` alongside the existing
`applyResult`: no error path (a cache hit is never a failure), doesn't set `mHasLoadedOnce` or
`mLastFolderEntries` (those must reflect only the authoritative refresh, so a subsequent
clear-search or reload never shows briefly-stale cached data). Safe to have both fire in quick
succession because `FileListModel::setEntries()` already does a full reset on every call
(Phase 6b).

Added `lcCache` logging category (`src/app/Logging.h`/`.cpp`), used only from `SqliteNodeCache.cpp`
— `FolderNavigationService` (SDK/Qt-free `MegaExplorerCore`) deliberately never logs, it just
silently discards a failed `saveChildren` `Result<void>`, since `SqliteNodeCache` already logged the
underlying cause via `lcCache` before returning failure. Cache failures are never surfaced through
`NotificationController`/`ErrorToast.qml` — a broken cache degrades silently to network-only
behavior, it's never a user-visible error.

**Gotchas**: `FileEntry` needed an `operator==` added (field-by-field, not `<=>`-defaulted since
`CMAKE_CXX_STANDARD` isn't pinned to C++20+) — gmock's `EXPECT_CALL(..., someVector)` implicitly
builds an `Eq()` matcher over `std::vector<FileEntry>`, which needs it. Mixing a catch-all
`ON_CALL(mock, method(_))` default with a narrower `EXPECT_CALL(mock, method(specificMatcher))` for
the same mock method doesn't work the way it looks: once *any* `EXPECT_CALL` exists for a method,
every call to it must match *some* registered `EXPECT_CALL` (checked most-recently-added first) —
`ON_CALL`'s default action only ever fires for calls gmock has nothing else to check against, not as
a fallback when a narrower `EXPECT_CALL` merely fails to match. Needed an explicit
`EXPECT_CALL(mock, method(_)).Times(AnyNumber())` registered *before* the specific one in
`GoBackCacheHitUsesTargetLocationKeyNotCurrentLocation`. Adding `SqliteNodeCache.cpp` to
`MegaExplorerTests` (for its own adapter test) pulled `Qt6::Core` and `src/app/Logging.cpp` into an
otherwise Qt-free test target, since `lcCache`'s actual `Q_LOGGING_CATEGORY` definition lives there.

## Phase 7a — session storage foundation (done)

First slice of Phase 7, deliberately decoupled from any actual login/MEGA-SDK wiring (deferred to
the next task list): `IMegaClient::loginWithSession` or equivalent wiring `dumpSession()`/
`fastLogin()` through `MegaSdkClient`, `main.cpp` composition-root changes, the login screen
itself, and logout/"forget session" UI are all still to come.

New port `ISessionStore` (`src/core/ISessionStore.h`) persists a single MEGA session token
(`MegaApi::dumpSession()`'s return value) across restarts: `loadSession()`/`saveSession()`/
`clearSession()`, synchronous like `INodeCache` (no async sibling to stay interface-consistent
with). Unlike `INodeCache::loadChildren`, there's no "never cached vs. cached empty" ambiguity: a
real session token is never legitimately an empty string, so `loadSession()` returns
`Result::ok("")` unambiguously for "nothing stored," kept distinguishable from `Result::fail`
(genuine read/decrypt failure).

Adapter `WindowsSessionStore` (`src/platform/WindowsSessionStore.h`/`.cpp`, alongside
`SqliteNodeCache`) encrypts the token at rest via Windows DPAPI (`CryptProtectData`/
`CryptUnprotectData`, current-user scope, `dwFlags=0`) plus a fixed app-specific entropy blob
passed as `pOptionalEntropy` — a pepper against another same-user process's bare
`CryptUnprotectData` call, not a secret (it ships in the binary; DPAPI's current-user scope is the
real security boundary). Simplifies MEGAsync's own `EncryptedSettings` approach (researched
read-only via `gh`/`curl` against the public repo, no code copied — see `CLAUDE.md`'s licensing
note): MEGAsync derives its entropy manually from the current user's SID plus extra XOR
obfuscation, but DPAPI's current-user scope already provides that same "only this Windows account
can decrypt it" guarantee, so the manual SID/XOR layering was skipped as redundant. Storage is a
dedicated file under `QStandardPaths::AppLocalDataLocation` (not `QSettings`/registry), same
non-roaming rationale as `MegaExplorer.log`/`node_cache.sqlite3`; resolving a real path
(`session.dat`) and constructing `WindowsSessionStore` in `main.cpp` is deferred to the next task
list along with everything else login-related.

Unlike `SqliteNodeCache`, no `mUsable` flag / permanently-broken-after-construction state: nothing
about DPAPI can fail at construction time (each `Crypt*` call happens per-operation, no
open/migrate step up front), so the constructor just stores the resolved path. `clearSession()`
always returns `Result::ok()` — deletes the file if present, but treats "file didn't exist" and
"the OS `remove()` call itself failed" identically as success, matching `SqliteNodeCache`'s
degrade-rather-than-fail ethos. Added `lcSession` logging category (`src/app/Logging.h`/`.cpp`),
used only from `WindowsSessionStore.cpp`.

Gets its own real adapter-level test (`tests/WindowsSessionStoreTest.cpp`), not just a
`MockSessionStore`-based one — DPAPI needs no live account/network, only a local Windows user
context, same reasoning as `SqliteNodeCacheTest.cpp`. Unlike sqlite3's `:memory:`, DPAPI has no
in-memory equivalent, so each test uses a real temp file under
`std::filesystem::temp_directory_path()`, cleaned up afterward.

**Gotcha**: a formatter hook silently reordered `<windows.h>`/`<wincrypt.h>` to alphabetical order
after every `Write`/`Edit` — even across a blank line separating them. `wincrypt.h` depends on
types (`ULONG_PTR`) declared by `windows.h`, so the reversed order broke the build with ~90
cascading syntax errors deep inside `wincrypt.h` itself, none of which mention the actual
include-order cause. Fixed with a `// clang-format off` / `// clang-format on` guard around just
those two lines — a bare blank line isn't enough to stop this formatter from merging and
re-sorting the block.
