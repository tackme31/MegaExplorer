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
| 6 | Local cache + open-folder background refresh | **next** (closes out MVP) |
| 7 | Login screen + session persistence | planned |
| 8 | Breadcrumb trail | planned |
| 9 | Folder tree navigation (side panel) | planned |
| 10 | Quick access (pinned folders, side panel) | planned |
| 11 | Rename / delete (move to rubbish) / move | planned |
| 12 | Multi-select + bulk operations | planned |
| 13 | Upload (drag & drop) | planned |
| 14 | In-app preview (side panel, `getPreview`) | planned |
| 15 | Real-time remote-change reflection | future, post-MVP |
| 16+ | Undecided | full bidirectional local sync stays out of scope |

### Phase 6 — local cache + open-folder background refresh

Not started. Persist the node tree locally (SQLite) and run a one-shot background refresh on folder
open — not continuous watching. Needs `src/platform`'s `IFileSystem` port (not yet created).

### Phase 7 — login screen + session persistence

`main.cpp` currently requires `MEGA_EMAIL`/`MEGA_PWD` env vars, no login UI. First post-MVP item:
foundational, independent of other features. Expected to use `dumpSession()`/`fastLogin(session)`;
where to persist the token (plain `QSettings` vs. guarded storage) still open. `IMegaClient` needs a
session-based login variant alongside `login(email, password, ...)`.

### Phase 8 — breadcrumb trail

Known gap since Phase 2. Cheap: `FolderNavigationService` already keeps a back-stack of
`Location`s, so mostly exposing existing state to QML.

### Phase 9 — folder tree navigation (side panel)

New left side panel; grouped with breadcrumb as "where am I / where can I go" nav, and becomes the
home for quick access (phase 10). Expected to reuse `IMegaClient::getChildren` for lazy expansion;
check performance on large folders.

### Phase 10 — quick access (pinned folders, side panel)

Adds a pinned-folders section to phase 9's panel. Persist via `Settings` (QtCore). Dangling-pin
handling (target deleted/moved) should be designed together with phase 11, not bolted on after.

### Phase 11 — rename / delete (move to rubbish) / move

Each maps to a single SDK request (`rename`/`moveToRubbish`/`moveNode`), no transfer-listener
needed. Prerequisite for phase 12. Mainly `FileContextMenu.qml` + matching `IMegaClient` methods.

### Phase 12 — multi-select + bulk operations

Sequenced after phase 11 (bulk ops need single-item versions first). Needs a selection model for
both `TableView` and `GridView`. Bulk download can likely reuse `DownloadService`'s existing queue.

### Phase 13 — upload (drag & drop)

Symmetric to phase 4's download but needs its own `MegaApi::startUpload` transfer listener
(mirroring `DownloadService`), higher cost than phase 11. Drag & drop via Qt Quick's `DropArea`.

### Phase 14 — in-app preview (side panel, `getPreview`/`startStreaming`)

Lowest priority; explicitly deferred in Phase 4 since download→open already covers "view the file".
Format-specific rendering (image/PDF/text/...) makes this the highest-effort item on the list.

### Phase 15 (future) — real-time remote-change reflection

Reflect other devices' changes via the SDK's push-notification mechanism into whatever listing is
open. Additive on top of phase 6's refresh.

### Phase 16+ — undecided

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
