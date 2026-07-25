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

## Phase 5 — thumbnails (in progress)

See `TASKS.md` for the current task breakdown and progress checklist. Scope agreed 2026-07-25:
server-generated thumbnails only (`MegaNode::hasThumbnail()` + `MegaApi::getThumbnail()`, no local
FreeImage/FFmpeg generation for files without one — extension-based fallback icon instead,
generation deferred to a later phase); in-memory-only cache (no persistence until Phase 6's SQLite
cache); list/grid view toggle persisted via QML's `Settings` (`import QtCore`, not the deprecated
`Qt.labs.settings`). This section gets filled in with the full implementation narrative (mirroring
Phases 0–4 above) after manual verification, as an independent follow-up commit — per `TASKS.md`'s
own last step. Don't assume it's done without checking `TASKS.md` and current file contents.
