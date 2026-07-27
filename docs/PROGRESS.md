# Project Progress Log

Detailed, phase-by-phase implementation history for MegaExplorer: what was built, why specific
decisions were made, and gotchas discovered along the way. `MEMO.md` has the high-level feature
list/roadmap (Japanese); `CLAUDE.md` has just a condensed current-status summary pointing here.
Read this file when you need the reasoning behind an existing implementation choice, not for
day-to-day build/edit work.

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
failure with no UI feedback, and there's no breadcrumb/current-path display.

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
Same known rough edge as Phase 2: search failure only `qWarning()`s, no UI feedback.

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
navigation/search error handling).
