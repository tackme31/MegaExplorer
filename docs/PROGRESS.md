# Project Progress Log

The roadmap (what's next, and why in this order) plus a detailed, phase-by-phase implementation
history for MegaExplorer: what was built, why specific decisions were made, and gotchas discovered
along the way. This file is now the single source of truth for the roadmap (moved here 2026-07-28
to end a dual-tracking drift with `docs/MEMO.md` — that file previously carried its own roadmap
section that kept falling out of sync with what was actually built). `docs/MEMO.md` keeps only
non-roadmap project notes (scope, tech stack, feature list, licensing, open technical concerns,
Japanese); `CLAUDE.md` has just a condensed current-status summary pointing here. Read the Roadmap
section below for what's planned and why it's ordered that way, and the phase sections further down
for the reasoning behind an already-built feature.

## Roadmap

Bottom-up development: each phase should be independently verifiable before the next one starts.
MVP = phases 0–6; phases 7+ are post-MVP extensions, sequenced by priority/dependency as reasoned
per-phase below (decided 2026-07-28, alongside the migration described above).

| # | Phase | Status |
|---|---|---|
| 0 | SDK build + CLI login | done |
| 1 | Bare file listing (root only) | done |
| 2 | Folder navigation | done |
| 3 | Search (MVP scope) | done |
| 4 | Download → open | done |
| 5 | Thumbnails + grid view | done |
| 6a | Categorized logging + error-toast feedback | done (pulled forward ahead of 6) |
| 6b | File-list attribute display + sort | done (pulled forward ahead of 6) |
| 6 | Local cache + open-folder background refresh | **next** (closes out the MVP) |
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

### Phase 6 (body) — local cache + open-folder background refresh

Not started. Closes out the original MVP definition: persist the node tree locally (SQLite, per
`docs/MEMO.md`'s tech-stack table) and run a one-shot background refresh whenever a folder is
opened — not continuous watching (see `CLAUDE.md`'s "What this project is"). `src/platform`'s
`IFileSystem` port (referenced but not yet created, see `docs/ARCHITECTURE.md`) is needed here.

### Phase 7 — login screen + session persistence

Decided 2026-07-28 (roadmap planning session), not yet implemented. Currently `main.cpp` requires
`MEGA_EMAIL`/`MEGA_PWD` environment variables and has no login UI or session persistence at all —
effectively a developer-only launch path. Prioritized first among the post-MVP items because it's
foundational usability, independent of the other new features, and should land before more UI
surface area (sidebar, preview panel, etc.) gets added on top of a dev-only entry point. Expected to
use the SDK's `dumpSession()`/`fastLogin(session)` pair to avoid a password prompt on every launch;
where the session token gets persisted (plain `QSettings` vs. something more guarded) is still an
open question. `IMegaClient` needs a session-based login variant alongside the existing
`login(email, password, ...)`.

### Phase 8 — breadcrumb trail

Planned. A known gap since Phase 2 ("no breadcrumb/current-path display", noted in that phase's log
below) that was never assigned to a phase. Cheapest item on this list: `FolderNavigationService`
already keeps a back-stack of `Location`s for the Back button, so this is mostly exposing that
existing state to QML rather than new domain logic.

### Phase 9 — folder tree navigation (side panel)

Planned. The most structurally significant of the new UI asks — a new left side panel. Grouped
right after the breadcrumb (phase 8) since both are "where am I / where can I go" navigation
features, and the side panel it introduces becomes the home for quick access (phase 10). Expected
to reuse `IMegaClient::getChildren` for lazy per-node expansion; no new SDK-facing port anticipated,
but expansion performance on large folders needs checking.

### Phase 10 — quick access (pinned folders, side panel)

Planned. Builds on phase 9's side panel by adding a pinned-folders section to it. Persisting pins is
small enough for `Settings` (QtCore), same mechanism as the sort/column-width state from phase 6b.
Needs to decide what happens to a pin when its target folder is later deleted/moved (phase 11) —
dangling-pin handling should be designed together with that phase, not bolted on afterward.

### Phase 11 — rename / delete (move to rubbish) / move

Planned. Classic Explorer file operations. Each maps to a single MEGA SDK request
(`MegaApi::rename`/`moveToRubbish`/`moveNode` equivalents) with no transfer-listener machinery
needed, unlike download/upload — lower implementation cost than phase 13's upload. Also the
prerequisite for phase 12: batch operations need something to batch. Mainly `FileContextMenu.qml`
additions plus the matching new `IMegaClient` methods.

### Phase 12 — multi-select + bulk operations

Planned. Deliberately sequenced after phase 11 — bulk delete/move/download only makes sense once
the single-item versions exist. Needs a selection model added to both `TableView` (currently only a
per-row `TapHandler`) and `GridView`. Bulk download can likely reuse `DownloadService`'s existing
one-at-a-time queue by enqueueing multiple jobs, rather than needing new queueing logic.

### Phase 13 — upload (drag & drop)

Planned. Symmetric to phase 4's download, but needs its own `MegaApi::startUpload`-based transfer
listener (mirroring `DownloadService`'s design) rather than a single request/response call, so
higher implementation cost than phase 11's file ops. Was already listed in `docs/MEMO.md`'s feature
list from the start but never assigned a phase until now. Drag & drop itself via Qt Quick's
`DropArea`.

### Phase 14 — in-app preview (side panel, `getPreview`/`startStreaming`)

Planned, lowest priority of the new items. Explicitly deferred during Phase 4 ("format-specific
in-app preview... considered and explicitly deferred", see that phase's log below) since download →
open in the default app already covers "view the file". Format-specific rendering
(image/PDF/text/etc.) makes this the highest-effort item on the list too.

### Phase 15 (future, post-MVP) — real-time remote-change reflection

Unchanged from the original roadmap (previously numbered phase 7): reflect other devices' changes
via the SDK's push-notification mechanism into whatever folder listing is currently open. Additive
on top of phase 6's open-folder refresh, so expected to have little impact on existing structure.

### Phase 16+ — undecided

Revisit as needed. Full bidirectional local sync (parity with the official desktop app) remains out
of scope for the foreseeable future.

## Phase 0 — SDK build & CLI login (done)

MEGA SDK (`meganz/sdk` v10.17.0) + `vcpkg` vendored as submodules under `third_party/`;
`appMegaExplorer` links `MEGA::SDKlib`; CLI login/fetchNodes verified.

## Phase 1 — bare file listing (done)

`IMegaClient`/`MegaSdkClient` (login/fetchNodes/getRootChildren) under `src/core`/`src/mega`;
`FileListingService` (`src/core`) chains login→fetchNodes→getRootChildren; `FileListModel`
(`src/qml`) bridges results into QML. Covered by `MockMegaClient`-based tests
(`tests/FileListingServiceTest.cpp`).

## Phase 2 — folder navigation (done)

`FolderNavigationService` (`src/core`) adds `openFolder`/`goBack` over `IMegaClient::getChildren`,
back-stack-based, with the root modeled as a sentinel `Location` (no real handle for root).
`FolderNavigationController` (`src/qml`) wraps both `FileListingService` (initial root load) and
`FolderNavigationService` (subsequent navigation) and is registered as the `controller` context
property in `main.cpp`, replacing the standalone `FileListModel` wiring. `Main.qml` has a real
`ListView` (delegate shows folder/file icon + name), double-click-to-open on folders via
`TapHandler.onDoubleTapped`, and a header `ToolBar` with a Back button gated on
`controller.canGoBack`. Covered by `tests/FolderNavigationServiceTest.cpp`. Known rough edges
(deferred, not blocking Phase 3): `FolderNavigationController::applyResult` only `qWarning()`s on
failure with no UI feedback (fixed in Phase 6a — see below), and there's no breadcrumb/current-path
display (still open).

## Phase 3 — search (done)

MVP scope decided 2026-07-25 — a search box in the header, default behavior is a **recursive
search scoped to the currently open folder** (`MegaSearchFilter::byName` +
`byLocationHandle(currentFolderHandle)` via `MegaApi::search()`, which is a synchronous call, not
listener/callback-based like login/fetchNodes). `IMegaClient::search` (`src/core`/`src/mega`)
mirrors `FolderNavigationService`'s root-sentinel `Location` convention; `SearchService`
(`src/core`) resolves the scope via a new `FolderNavigationService::currentLocation()` getter and
is SDK-free/unit-tested like the other core services (`tests/SearchServiceTest.cpp`).
`FolderNavigationController::search(QString)` runs it and swaps `FileListModel`'s contents without
touching navigation state (back-stack/`canGoBack`); an empty query restores the last folder
listing from a cached member instead of re-fetching. `Main.qml` has a `TextField` in the header
`ToolBar`, submitting on Enter only (`MegaApi::search()` blocks the GUI thread synchronously). A
filter button beside the search box, deferred to a later pass, will expose the rest of
`MegaSearchFilter` (account-wide search via `byLocation(SEARCH_TARGET_ALL)`, `byCategory`,
`byCreationTime`/`byModificationTime`, `byFavourite`, `bySensitivity`, `byTag`/`byDescription`)
plus `MegaSearchPage`-based pagination — none of that was needed for the MVP search box itself.
Same known rough edge as Phase 2: search failure only `qWarning()`s, no UI feedback (fixed in
Phase 6a — see below).

## Phase 4 — download → open (done)

Hybrid flow — file double-click and a new right-click context menu's "ダウンロード" item both call
`DownloadController::downloadFile(handle, name, sizeBytes)` (`src/qml`, new `downloadController`
context property, separate from `FolderNavigationController` since it's an independent concern),
so they funnel into the same flow instead of diverging. `IMegaClient::download`
(`src/core`/`src/mega`) diverges from the rest of the interface's single-`Result<T>`-callback
shape: `MegaApi::startDownload` is `MegaTransferListener`-based, so it takes a progress callback
plus a `Result<DownloadOutcome>` completion callback (`DownloadOutcome`,
`src/core/DownloadOutcome.h`: `localPath` is the actual saved path, since
`COLLISION_RESOLUTION_NEW_WITH_N` can rename on a name collision; `alreadyPresent` is true when an
identical — fingerprint-matching — file already existed at the destination and the SDK skipped the
transfer outright rather than writing anything, per `COLLISION_CHECK_FINGERPRINT`). Confirmed by
tracing `third_party/sdk`'s collision-resolution path (`megaapi_impl.cpp`'s `CollisionChecker`/
`CompleteFileDownloadBySkip`, `filesystem.cpp`'s
`FileDistributor::moveToForMethod_RenameWithBracketedNumber`, which calls Win32 `MoveFileExW`
*without* `MOVEFILE_REPLACE_EXISTING`) that this combination never actually overwrites a
same-named file with different content — it renames the new file with a `(1)`/`(2)`/... suffix
instead. `alreadyPresent` is inferred in `MegaSdkClient`'s `DownloadListener` (no public SDK getter
exists for the collision-check outcome) from
`transfer->getTransferredBytes() == 0 && transfer->getTotalBytes() > 0` at completion, since
`CompleteFileDownloadBySkip` explicitly zeroes `transferredBytes` on the skip path. Added
2026-07-25 after a user report of "same-name downloads overwrite the existing file" turned out to
be this skip case surfacing no visible difference in the UI (`DownloadSnackbar.qml` showed the same
generic "ダウンロード完了" message either way) rather than an actual overwrite — the snackbar now shows
"既にダウンロード済みです" instead when `alreadyPresent` is true. The renamed-file case (different content,
same requested name) is also covered: `DownloadController` reports the *actual* saved leaf name
(`QFileInfo(resolvedLocalPath).fileName()`) as the snackbar's `fileName` on success, not the
originally-requested `job.name` — so a collision-renamed download reads as e.g. "photo (1).jpg の
ダウンロードが完了しました" instead of silently claiming "photo.jpg" completed, which is what read as an
overwrite. `MegaSdkClient` implements it with a self-deleting `DownloadListener`, matching the
existing `LoginListener`/`FetchNodesListener` idiom. `DownloadService` (`src/core`) serializes
downloads one at a time over that port, auto-advancing the queue on completion; built with a
per-job id/state (`DownloadJob`, `jobs()` snapshot) from the start so a future progress-list UI and
per-job cancel can be added without an API change, even though this pass only surfaces the single
active job to QML (`DownloadController`'s `downloadActive`/`activeFileName`/`activeProgress`
properties, shown in a footer `ToolBar`). SDK-free/unit-tested like the other core services
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

## Phase 5 — thumbnails (done)

Scope agreed 2026-07-25: server-generated thumbnails only (`MegaNode::hasThumbnail()` +
`MegaApi::getThumbnail()`); no local FreeImage/FFmpeg generation for files without one —
extension-based fallback icon (existing 📁/📄 glyphs) instead, generation deferred to a later
phase; in-memory-only cache (no persistence until Phase 6's SQLite cache); list/grid view toggle
persisted via `QtCore`'s `Settings` type, not the deprecated `Qt.labs.settings` (Qt 6.5 doc
explicitly says to use the former).

`FileEntry::hasThumbnail` (`src/core/FileEntry.h`) and `IMegaClient::getThumbnail` (single
`Result<std::string>`-callback, like `login`/`fetchNodes` — `MegaApi::getThumbnail` is
`MegaRequestListener`-based, unlike `download`'s two-callback `MegaTransferListener` shape) round
out the port. `MegaSdkClient::getThumbnail` follows the same self-deleting-listener idiom as
`LoginListener`/`FetchNodesListener`/`DownloadListener` (new `ThumbnailListener`), reusing
`resolveNode` to fail fast on an unknown handle; `nodeListToEntries` now also copies
`node->hasThumbnail()` into `FileEntry`.

`ThumbnailService` (`src/core`, SDK-free like the other core services,
`tests/ThumbnailServiceTest.cpp`) sits in front of the port with three behaviors `DownloadService`
doesn't need: a `handle → local path` success cache (thumbnails don't change once fetched, so
unlike downloads there's no reason to ever re-run a successful job); in-flight de-duplication
(concurrent requests for the same handle attach to the one job instead of firing the SDK call
twice — e.g. list/grid toggling back and forth before a fetch completes); and bounded
*parallel* execution (cap of 4 concurrent jobs, vs. `DownloadService`'s strict one-at-a-time) since
thumbnails are small enough that serializing them would make grid scrolling feel slow. Failures
aren't cached, so a later request for the same handle gets a fresh attempt. Same mutex-around-state
/ lock-free-SDK-call split as `DownloadService`, for the same reason (`MockMegaClient` fires
callbacks synchronously, so calling `IMegaClient::getThumbnail` while holding the lock would
self-deadlock).

`FileListModel` (`src/qml`) gained `HasThumbnailRole`/`ThumbnailPathRole` (empty string until
filled) and `setThumbnailPath(handle, path)`, which does a linear search + per-row `dataChanged()`
rather than `beginResetModel()`, to avoid flicker while the grid is scrolling as thumbnails stream
in. Wiring the fetch result back into that same visible model instance is why `ThumbnailController`
(`src/qml`, new `thumbnailController` context property) is coupled to `FileListModel` in a way
`DownloadController` deliberately isn't — a download doesn't need to mutate the row it came from,
a thumbnail display does. `FolderNavigationController` exposes the shared `FileListModel*` via a
non-`Q_INVOKABLE` typed getter for `main.cpp` to wire the two controllers together at construction
time. `ThumbnailController::requestThumbnail(handle)` resolves a destination path under
`QStandardPaths::TempLocation` (app-specific subfolder, `QDir::toNativeSeparators()`d like
`DownloadController`'s path handling), calls `ThumbnailService::request`, and marshals the
completion back to the GUI thread before calling `setThumbnailPath`. QML only calls it for rows
where `hasThumbnail && !isFolder`, to avoid pointless SDK round-trips.

`Main.qml`: `StackLayout { currentIndex: window.viewMode }` holds a `ListView` and `GridView` side
by side (the inactive one is automatically non-visible/non-interactive); double-click open/download
dispatch was pulled into a shared `window.activateEntry(...)` function and the right-click menu into
an inline `component FileContextMenu: Menu {...}` so both delegates share behavior. Gotcha: an
inline `component` can't be a sibling *before* the root object in a `.qml` file — `qmlcachegen`
treats that as a syntax error — it has to be declared as a direct child *inside* the root
(`ApplicationWindow`). The grid delegate's `Image.source` is built from `ThumbnailController`'s
(Windows-native, backslash-separated) path with `thumbnailPath.replace(/\\/g, "/")` before
prepending `"file:///"`, since a literal backslash path breaks URL parsing. The footer `ToolBar`
lost its `visible: downloadController.downloadActive` binding (now always shown, as an
Explorer-style status bar) — download progress keeps its own `visible` binding on the
label/progress-bar pair, with an `Item` spacer taking over when it's hidden so the list/grid toggle
icons (plain `"☰"`/`"⊞"` glyphs, no icon font in this project yet) stay pinned to the right edge.
`property alias viewMode: window.viewMode` inside a `Settings { }` element persists the toggle
(an alias auto-persists on every change; a plain property would only load the initial value).
No `CMakeLists.txt` change was needed for `Settings` — it's part of the built-in `QtCore` QML
module shipped by qtdeclarative itself, already reachable via `Qt6::Qml` (which `Qt6::Quick`
already depends on).

Manual verification (2026-07-27): mixed thumbnail/no-thumbnail folders render correctly, list/grid
toggle survives an app restart, rapid scrolling through a large folder doesn't trip API rate
limits (the 4-way concurrency cap holds), and the build is clean at `/W4` with zero warnings.
Known deferred gaps, unchanged from `TASKS.md`'s scope notes: no thumbnail-request cancellation
(same limitation `DownloadService` has), no richer per-extension icon set, no UI feedback on
thumbnail fetch failure (falls back to the generic icon silently, same as Phase 2/3's
navigation/search error handling at the time — fixed in Phase 6a below).

## Phase 6a — categorized logging + unified error-toast feedback (done)

A slice of Phase 6's "エラーハンドリング全般" scope, pulled forward and done before the rest of
Phase 6 (local cache, open-folder background refresh) — decided 2026-07-28. Two independent
problems prompted it: three of the four failure paths above (`FolderNavigationController`'s
`applyResult`/`applySearchResult`, `ThumbnailController::requestThumbnail`) and
`DownloadController::openFile`'s OS-open failure surfaced nothing to the user, only a bare
`qWarning()`; and `appMegaExplorer` builds `WIN32_EXECUTABLE TRUE`, so that `qWarning()` output
reached no visible destination at all on a normal launch — there was effectively no record of any
failure anywhere. A pre-existing bug was also found and fixed in passing:
`DownloadService.cpp`'s failure branch copied `Result<DownloadOutcome>::errorMessage` into
`DownloadJob` but silently dropped `errorCode` (no field existed for it).

Scope agreed 2026-07-28: Qt-native logging only (`QLoggingCategory` + `qInstallMessageHandler`), no
third-party logging library; MEGA SDK's own `MegaLogger` hook included in this pass (previously
completely unwired, so SDK-internal network/transfer diagnostics went nowhere); simple
single-generation log rotation, no size cap; a new generic error-toast UI wired only into the three
previously-silent paths plus `openFile`'s sub-path — `DownloadSnackbar`'s own success/fail flow left
untouched, not refactored into the same mechanism.

New `src/app/` directory (a new top-level category alongside `src/core`/`src/mega`/`src/qml`/
`src/platform`) holds `Logging.{h,cpp}`: `Q_DECLARE_LOGGING_CATEGORY`/`Q_LOGGING_CATEGORY` per
functional area (`lcApp`/`lcNavigation`/`lcSearch`/`lcDownload`/`lcThumbnail`/`lcSdk`), all using the
plain 2-arg macro form (verified against Qt's own docs: only `qt.`-prefixed categories get a
debug/info-suppressed default — a custom category name like these already defaults to "all message
types enabled", so no third `QtWarningMsg`-floor argument was needed anywhere, including `lcSdk`).
`installLogging()` sets a message pattern via `qSetMessagePattern`, then installs a
`qInstallMessageHandler` that formats each line with `qFormatLogMessage` and writes it to both
`stderr` (keeps Qt Creator's Application Output pane working) and a log file under
`QStandardPaths::AppLocalDataLocation` (`MegaExplorer.log`) — not `AppDataLocation`: on Windows the
latter is the *roaming* profile path, which would sync a growing log file over the network on every
logon/logoff in a domain environment for no benefit; `AppLocalDataLocation` resolves to the exact
same path as `AppDataLocation` on macOS/Linux per Qt's own docs, so this only changes behavior on
Windows. Guarded by a `QMutex` since the handler can
be called from MEGA SDK-internal background threads (via the bridge below) as well as the GUI
thread. Rotation is single-generation: any existing log from a previous run is renamed to
`MegaExplorer.log.1` on startup, nothing older is kept. Must run before any other logging call —
installed in `main.cpp` right after `setOrganizationName`/`setApplicationName` (which
`AppDataLocation` depends on) and before the pre-existing env-var-check `qWarning()`, now
`qCWarning(lcApp)`. Deliberately kept out of `MegaExplorerCore`/`src/core` to preserve that
library's Qt-free-ness — it's `appMegaExplorer`-only infrastructure, same as the QML-facing
controllers.

`MegaSdkLogger` (`src/mega`, not `src/app`, since deriving from `mega::MegaLogger` requires
including `megaapi.h` — the "only `MegaSdkClient` may touch `megaapi.h`" rule from earlier phases
widened to "only files under `src/mega`") bridges the SDK's own logger callback into the `lcSdk`
category (`FATAL`/`ERROR`/`WARNING` → `qCWarning`, `INFO` → `qCInfo`, `DEBUG`/`VERBOSE` →
`qCDebug`), registered via the static `MegaApi::addLoggerObject`/`removeLoggerObject` in
`MegaSdkClient`'s constructor/destructor (previously `= default`, now real bodies). No volume
concern in practice: the SDK's own `MegaApi::setLogLevel` defaults to `LOG_LEVEL_INFO`, so it never
emits `DEBUG`/`VERBOSE` unless that's explicitly raised.

`NotificationController` (`src/qml`, alongside the other controllers) is the shared UI-facing half:
one `errorOccurred(QString context, QString errorMessage)` signal, no text formatting of its own —
`context` (`"navigation"`/`"search"`/`"thumbnail"`/`"openFile"`) plus the raw message let
`ErrorToast.qml` compose the localized sentence per context, the same "C++ passes structured
fields, QML composes the text" convention `DownloadSnackbar` already established. Constructed once
in `main.cpp` (`NotificationController notifications;`, declared *before* the three controllers
below it since they hold a non-owning raw pointer to it and C++ destroys stack locals in reverse
construction order) and threaded via constructor injection into `FolderNavigationController`,
`ThumbnailController`, and `DownloadController` — all three gained a new trailing constructor
parameter, the only call sites being `main.cpp`'s composition root. Each of the four failure sites
now does `qCWarning(category) << ...` (replacing the old bare `qWarning()`) followed by
`notifyError(...)`, except `DownloadController`'s actual download-failure path (inside
`setOnJobFinished`), which gained a `qCWarning(lcDownload)` log line it never had before (for
log-file parity with the other three paths) but deliberately no `notifyError()` call —
`DownloadSnackbar` already surfaces that failure, and touching its flow was explicitly out of
scope.

`qml/components/ErrorToast.qml` mirrors `DownloadSnackbar.qml`'s existing visual convention exactly
(bottom-anchored `Popup`, 6s auto-hide `Timer`) rather than inventing a new one, wired into
`Main.qml` alongside `downloadSnackbar` via one more instance + one more `Connections` block.

`DownloadJob` (`src/core/DownloadService.h`) gained an `errorCode` field alongside the existing
`errorMessage`, populated in `DownloadService.cpp`'s failure branch the same way; the existing
`tests/DownloadServiceTest.cpp` failure-path test (renamed
`EnqueueFailurePropagatesErrorMessageCodeAndState`) now also asserts on it. This is the only change
inside `MegaExplorerCore` in this whole phase — a plain `int`, no new Qt dependency, so the
library's Qt-free-ness holds.

Nothing added a new `find_package`/vcpkg dependency (`QLoggingCategory`/`qInstallMessageHandler`/
`QFile`/`QStandardPaths`/`QMutex` are all already reachable via the existing `Qt6::Quick` link), and
`tests/CMakeLists.txt` needed no change at all.

## Phase 6b — file-list attribute display + sort (done)

Scope agreed 2026-07-28, done the same day (pulled forward ahead of Phase 6's body, same
"differential slice" reasoning as Phase 6a above). List-view mode changed from the previous
single-column name-only `ListView` to an Explorer-style detail view: sortable Name / Date modified /
Size columns, folders always sorted first — per the SDK's own documented guarantee ("the nodes are
always sorted by type, being folders always first"), so no client-side folders-first logic was
needed. A "Kind" column was considered and dropped: it would need extension-based classification
done client-side, which conflicted with the sort-server-side decision below (`MegaApi::getChildren`/
`search`'s `order` has no value corresponding to a Kind column) — the `FileKind.h/.cpp` helper and
`FileListModel::ExtensionRole` built for it were removed again. Grid/thumbnail view (Phase 5) is
untouched.

Sorting is done server-side via `MegaApi::getChildren`/`search`'s `order` argument, not client-side
in memory, to hold up under folders with tens of thousands of entries. New `src/core/SortOrder.h`
(`SortKey::{Name,Size,ModificationTime}` + `ascending`, Qt-free like the rest of `src/core`) is
threaded through `IMegaClient`/`MegaSdkClient`, which maps it to
`MegaApi::ORDER_DEFAULT_ASC/DESC`/`ORDER_SIZE_ASC/DESC`/`ORDER_MODIFICATION_ASC/DESC`
(`MegaSdkClient.cpp`'s `toMegaOrder`). `FolderNavigationController::setSortOrder(column, ascending)`
is the header-click entry point; it keeps the chosen `SortOrder` as member state and re-fetches
either the current search results or the current folder listing (`FolderNavigationService::
refreshCurrent`, back-stack untouched) depending on whether a search is active, so sort stays
applied to whichever listing is currently shown, search included.

List rendering moved from `ListView`+`QAbstractListModel` to `TableView`+`HorizontalHeaderView`
(`import QtQuick.Controls`), which required rewriting `FileListModel` from a single-column
role-per-file model to a `QAbstractTableModel`-shaped one (`columnCount()` returns 3, `data(index,
role)` switches on `index.column()`). New `qml/views/FileTableView.qml` owns the table; column
header labels are hardcoded there rather than sourced from `headerData()` — same "C++ passes
structured fields, QML composes user-facing text" convention `NotificationController`/
`ErrorToast.qml` established, which also sidesteps an MSVC-codepage gotcha with Japanese literals in
`.cpp`/`.h` files (moot in the end since the UI strings landed in English — see below — but the
convention itself predates and is unrelated to that later decision).

A few `FluentWinUI3`-specific gotchas surfaced building this: `HorizontalHeaderView` ships no
default delegate under `FluentWinUI3` (unlike `Basic`), so its delegate's `Rectangle` needed an
explicit `color: "transparent"` — left at `Rectangle`'s default opaque white, the Label's
theme-driven foreground came out white-on-white and invisible. `textRole` needed to be explicitly
set to `""` since it defaults to `"display"`, which `FileListModel::roleNames()` doesn't provide
(the header delegate builds its text from a hardcoded `columnLabels` array, never via `textRole`),
otherwise Qt logs a "role doesn't exist" warning on every load. Both `TableView` and
`HorizontalHeaderView` are `Flickable`-derived and defaulted to click-drag panning, unexpected for
an Explorer-style list — fixed with `acceptedButtons: Qt.NoButton` on both (`Flickable.
acceptedButtons`, since Qt 6.9), which leaves wheel scrolling untouched.

Column resizing uses `TableView.resizableColumns: true` (Qt 6.5+, standard support, no custom drag
handling needed). Widths and sort column/direction both persist across restarts via `Settings`
(QtCore), same mechanism as Phase 5's view-mode toggle. Column widths need reapplying not just once
at `Component.onCompleted` but also on every `TableView.onRowsChanged`: `FileListModel::
setEntries()` does a full `beginResetModel()`/`endResetModel()` on every folder navigation, and
`TableView` doesn't guarantee explicitly-set column widths survive a model reset.
`saveColumnWidths()` is wired to `TableView.onLayoutChanged` (the only official resize-related hook
`TableView` exposes, despite also firing on scroll — safe here since a same-value QML property
assignment is a no-op) and clamps to a `minColumnWidths` floor so a dragged-in column can't shrink
to unreadable/zero width.

The Date modified column matches (English) Windows Explorer's own format — short date + short time,
e.g. "7/28/2026 3:45 PM" — which isn't reproducible via `Qt.formatDateTime(date,
Locale.ShortFormat)`: `QLocale`'s `ShortFormat` for en_US uses a 2-digit year (`"M/d/yy h:mm AP"`),
while Explorer's regional short-date default is 4-digit. The format string is hand-written
(`"M/d/yyyy h:mm AP"`, wrapped in `qsTr()` so a future Japanese `.ts` file could supply the
equivalent Explorer format, typically 24-hour with no AM/PM). `AP` is Qt's AM/PM token — easy to
confuse with `tt`, which is a *timezone* token in Qt's format strings despite meaning AM/PM in
.NET/Windows format syntax. Folders show a blank Date modified cell rather than the Unix epoch:
`MegaNode::getModificationTime()` returns 0 for folders, and formatting that verbatim would have
shown 1970-01-01, unlike real Explorer.

All list/table UI strings (column labels, date format) ended up in English during this phase,
consistent with the rest of the shipped UI (`Main.qml`'s existing "← Back"/"Search in this folder"
etc.) — the project has no localization infrastructure yet, so "Japanese-language UI" was never
actually a hard requirement despite `docs/MEMO.md`/`CLAUDE.md` being written in Japanese/English
respectively.

Manual verification confirmed sort-direction toggling (re-click the same header), folders staying
pinned first regardless of sort column, column-width and sort-order persistence across an app
restart, and a clean `/W4` build.
