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
| 6 | Local cache + open-folder background refresh | done (closes out MVP); cache removed in 7b |
| 7a | Session storage foundation (`ISessionStore`/`WindowsSessionStore`) | done |
| 7 | Login screen + session persistence (remaining wiring) | done |
| 7b | Remove local node cache (`INodeCache`/`SqliteNodeCache`) | done |
| 8 | Breadcrumb trail | done |
| 9 | Windows-Explorer-style tabs (multiple folder views) | planned |
| 10 | Folder tree navigation (side panel) | planned |
| 11 | Quick access (pinned folders, side panel) | planned |
| 12 | Rename / delete (move to rubbish) / move | planned |
| 13a | Selection model (row/cell, keyboard, right-click) | done (pulled forward) |
| 13 | Multi-select + bulk operations | planned (selection model prerequisite done in 13a) |
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

### Phase 7b — remove local node cache

`INodeCache`/`SqliteNodeCache` (Phase 6) proved buggy in practice and not worth the debugging cost,
so it was removed entirely: `FolderNavigationService`/`AuthService` go back to network-only
behavior, and `FolderNavigationService::openRoot`/`openFolder`/`goBack` collapse from a two-callback
cache-then-refresh shape back to the same single-callback shape `refreshCurrent` already had. A
loading/busy indicator to cover the resulting per-navigation network latency is deliberately out of
scope here — tracked as a separate follow-up task. See the Phase 7b implementation-log entry below
for what was built.

### Phase 8 — breadcrumb trail

Known gap since Phase 2. Note: `mBackStack` is a *history* stack, not an ancestor chain (opening a
deep folder from search results, which `FolderNavigationController` explicitly allows, makes them
diverge), so the breadcrumb can't be read off it directly -- it's resolved from the SDK's node tree
instead. See the Phase 8 implementation-log entry below for what was built.

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
both `TableView` and `GridView` — built ahead of schedule as phase 13a (see below and its
implementation-log entry). The selection-driven context menu plus a declarative
target/arity action-resolution table were also pulled forward as phase 13b (see its
implementation-log entry) since bulk download doesn't actually depend on phase 12 — it reuses
`DownloadService`'s existing queue directly. Remaining scope: rename/delete/move as multi-item
context-menu actions, which do need phase 12's single-item versions first (they slot into 13b's
resolver table as new `FileActionSpec` rows, no resolver redesign needed).

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
`textRole` was originally set to `""` here to silence a "role doesn't exist" warning (the header
delegate builds its text itself from `columnLabels`, never via `textRole`) — **correction**: this
didn't actually work, `""` doesn't count as "set" for Qt's existence check and it still fell back to
`"display"` internally; fixed in the phase 13a slice by pointing it at an actual existing role
(`"name"`) instead, see that implementation-log entry. Both `TableView`/`HorizontalHeaderView`
default to click-drag panning — fixed with `acceptedButtons: Qt.NoButton` (Qt 6.9+), wheel scroll
unaffected.

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

**Superseded by Phase 7b** (below): this cache proved buggy in practice and was removed entirely;
kept here as a historical record of what Phase 6 built and why, not as a description of current
behavior.

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

## Phase 7 — login screen + session persistence (done)

Split into two slices, landed as separate commits: a backend slice (`AuthService`, `MegaErrorCodes.h`,
`IMegaClient`/`INodeCache` extensions, the `MegaSdkClient` adapter work) and this QML/UI wiring
slice. `AuthService` (`src/core`) coordinates login/restore/logout and session-token persistence
over `IMegaClient`/`ISessionStore`/`INodeCache` — deliberately not depending on
`FolderNavigationService` itself, so resetting navigation state on logout/re-login stays the QML
layer's job (`FolderNavigationController::reset`, see below), not this service's. `MegaSdkClient`
gained `loginWithSession`/`multiFactorAuthLogin`/`logout`/`currentSessionToken`
(`fastLogin`/`multiFactorAuthLogin`/`logout(false, ...)`/`dumpSession()`); its near-duplicate
`LoginListener`/`FetchNodesListener` were merged into one `SimpleResultListener`. `logout`'s
`false` first argument requires `ENABLE_SYNC` to be defined (already a `PUBLIC` define via the
vendored SDK's own CMake, so no build-system change needed).

`AuthController` (`src/qml`, new) wraps `AuthService` for QML: a 7-state `AuthState` machine
(`Restoring`/`LoggedOut`/`LoggingIn`/`NeedsTwoFactor`/`VerifyingTwoFactor`/`LoggedIn`/`LoggingOut`)
plus an `AuthErrorKind` enum, both `Q_ENUM`. This is the codebase's first `QML_ELEMENT`/
`QML_UNCREATABLE` type registration — every other controller is exposed purely via
`setContextProperty`; `AuthController` needs actual QML-visible enum *type names*
(`AuthController.LoggedIn`), which requires real QML type registration, so it's both a context
property *and* a registered (but uncreatable — QML never constructs one, main.cpp still owns the
instance) type. Deliberately has no `NotificationController*` dependency, unlike every other
controller here: login/2FA failures render inline on `LoginView.qml` via `authErrorKind`/
`rawErrorMessage` (only populated for the catch-all `UnknownError` case — every other kind maps to
a fixed, localized sentence composed in QML, same "C++ passes fields, QML composes text"
convention as `NotificationController`/`ErrorToast.qml`), and `logout()` always succeeds from the
caller's perspective (`AuthService::logout`'s own contract), so there's no failure path left that
needs the global toast. A wrong 2FA PIN is folded into `InvalidCredentials` from
`kENoEnt`/`kEFailed`/`kEExpired` — `megaapi.h` doesn't document a distinct "wrong PIN" code, so this
is a best guess pending confirmation against a real 2FA account.

`FolderNavigationController` lost its `FileListingService` dependency: `loadRoot()` became
`Q_INVOKABLE` (called from `Main.qml`'s `Connections` on `authController.authStateChanged` reaching
`LoggedIn`, rather than once from `main.cpp` before `app.exec()`) and now calls
`FolderNavigationService::openRoot` directly. Gained `reset()` (also `Q_INVOKABLE`, called on
`authStateChanged` reaching `LoggedOut`): clears the file list, cached folder entries, search
query, and `mHasLoadedOnce`, plus `FolderNavigationService::resetToRoot()` (from the backend slice)
— so a subsequent login, possibly to a different account, never briefly shows the previous
account's cached listing or retains its back-stack handles. `FileListingService` (`.h`/`.cpp` +
its test) is deleted outright: `AuthService` now owns the login→fetchNodes chain, and
`FolderNavigationController::loadRoot` calls `openRoot` itself, so the thin orchestration layer
`FileListingService` used to provide has no remaining reason to exist.

`main.cpp` drops the `MEGA_EMAIL`/`MEGA_PWD` env-var requirement entirely. Composition root now
also builds a `WindowsSessionStore` (same `AppLocalDataLocation` cache dir as `node_cache.sqlite3`,
sibling file `session.dat`) and an `AuthService`, and constructs `AuthController` alongside the
other controllers. `engine.loadFromModule(...)` still runs before `authController.restoreSession()`
is called — same deliberate ordering Phase 6 established for `controller.loadRoot()` (QML's
`Component.onCompleted` runs first), just moved to the new call.

`qml/views/LoginView.qml` (new) switches between an email/password step and a 6-digit 2FA step
based on `authController.authState`, with a `BusyIndicator` + "Signing you in…" shown only during
`Restoring` (no dedicated splash screen). `Main.qml`'s header/footer/central content are now
`Loader`-driven — this codebase's first `Loader` use, chosen over `StackView` because it's an
exclusive two-state switch, not a multi-step screen flow. `header:`/`footer:` are themselves
`Loader { active: authController.authState === AuthController.LoggedIn }` (an inactive `Loader` has
zero size, collapsing the chrome away on the login screen); the central area is a third `Loader`
alternating between the pre-existing `StackLayout` and `LoginView`. A new header `≡` `ToolButton` →
`Menu` → "Sign out" `MenuItem` opens a confirmation `Dialog` (also this codebase's first) that
warns if a download is active (`DownloadService` has no cancel API yet, so `logout()` simply aborts
it) before calling `authController.logout()`.

**Gotchas**: `qt_add_qml_module`'s generated `<target>_qmltyperegistrations.cpp` `#include`s each
`QML_ELEMENT` header by *bare filename* with angle brackets (`#if __has_include(<AuthController.h>)`),
not by the path given in `SOURCES` (`src/qml/AuthController.h`) — since `appMegaExplorer`'s include
path only had `src/` on it (so existing code spells the include `"qml/AuthController.h"`), the bare
`<AuthController.h>` failed to resolve and every generated `qmlRegisterTypesAndRevisions`/
`qmlRegisterEnum` call in that file errored out as referencing an undefined type. Fixed by adding
`src/qml` itself to `appMegaExplorer`'s `target_include_directories`, alongside the existing `src`.
Separately, once that resolved, `/W4` started flagging two `C4702` ("unreachable code") warnings
inside Qt's own `qjsengine.h`/`qvariant.h` — template instantiations pulled in for the first time by
`qmlRegisterTypesAndRevisions<AuthController>`, not reachable/fixable from application code; left as
an accepted, vendor-header-only warning pair rather than something to chase.

**Known limitations**: `AuthService::logout` clears `ISessionStore`, but not the in-memory
thumbnail cache (`ThumbnailService`) or `DownloadService`'s job queue — neither is exposed to
`AuthService`, and both are already short-lived/harmless to leave stale across a sign-out in this
single-window app. The "wrong 2FA PIN" error-code mapping above is unconfirmed against a real
account pending manual smoke testing.

## Phase 7b — remove local node cache (done)

The Phase 6 SQLite node-tree cache (`INodeCache`/`SqliteNodeCache`) proved buggy in practice and not
worth the debugging cost, so it was removed entirely rather than fixed. A loading/busy indicator to
cover the resulting per-navigation network latency is a deliberate follow-up, out of scope here.

Deleted outright: `src/core/INodeCache.h`, `src/platform/SqliteNodeCache.h`/`.cpp`,
`tests/MockNodeCache.h`, `tests/SqliteNodeCacheTest.cpp`. `FolderNavigationService` lost its `mCache`
member/constructor parameter; `openRoot`/`openFolder`/`goBack` collapsed from a two-callback
(`onCacheHit`/`onRefreshed`) shape back to the single-callback `onDone` shape `refreshCurrent` always
had, and the private `loadWithCache` helper was trimmed to `runAndCommit` (network call → commit
state on success → `onDone`, no cache read/write-through). `AuthService::logout` no longer clears a
node cache — just `ISessionStore::clearSession()`. `FolderNavigationController` (`src/qml`) lost
`applyCacheHit`; `loadRoot`/`openFolder`/`goBack` now wire a single lambda straight into
`applyResult`. `main.cpp`'s composition root drops `SqliteNodeCache` construction and the
`nodeCache` injection into `FolderNavigationService`/`AuthService` (the `cacheDir` local itself
stays — `WindowsSessionStore` still needs it for `session.dat`). Removed the `lcCache` logging
category (`src/app/Logging.h`/`.cpp`) since nothing logs through it anymore.

Root `CMakeLists.txt` dropped `find_package(unofficial-sqlite3 CONFIG REQUIRED)` and the
`unofficial::sqlite3::sqlite3` link on `appMegaExplorer`, along with `INodeCache.h`/
`SqliteNodeCache.h`/`.cpp` from the relevant source lists; `tests/CMakeLists.txt` similarly dropped
`SqliteNodeCacheTest.cpp`, `SqliteNodeCache.cpp`, `MockNodeCache.h`, and the sqlite3 link. Six
cache-specific tests in `tests/FolderNavigationServiceTest.cpp` were deleted outright (they asserted
cache-hit/write-through behavior that no longer exists); the remaining tests, plus all of
`tests/AuthServiceTest.cpp`, were updated to construct their services without a mock node cache and
to call the new single-callback `FolderNavigationService` API.

## Phase 13a — selection model: row/cell, keyboard, right-click (done)

Pulled forward out of order: phase 13's roadmap entry already called out needing "a selection model
for both `TableView` and `GridView`" as a prerequisite for its bulk operations, and that slice is
self-contained (no dependency on phases 8–12), so it was built now rather than waited on. Bulk
operations themselves (multi-item context-menu actions, etc.) are still deferred to phase 13.

`FileListModel` gains `selectRow`/`clearSelection`/`selectAll`/`moveCursor`/`cursorRow`, a
`SelectedRole`, and a `selectedHandles` `Q_PROPERTY` exposing `std::unordered_set<quint64>
mSelectedHandles` to QML (plus a typed `selectedHandleSet()` accessor for future non-QML consumers,
e.g. a delete/move controller). Selection tracks three handle-keyed (not row-index-keyed — handles
are globally unique per MEGA node, survive a same-folder re-sort, and cleanly don't survive
navigation/search) pieces of state, each `std::optional<quint64>`:

- `mSelectedHandles` — the selected set itself.
- `mAnchorHandle` — fixed at the last plain/Ctrl click (or the target row on a right-click that
  changes selection); every Shift-range operation, mouse or keyboard, spans from this anchor rather
  than from the cursor, so repeated Shift+click/Shift+arrow grows or shrinks the same range instead
  of chasing the last touched row.
- `mCursorHandle` — the keyboard-navigable "last touched" row, moved by every operation *including*
  Shift (unlike the anchor). `cursorRow()` (`Q_INVOKABLE`, deliberately not a `Q_PROPERTY` — nothing
  binds to it, QML just reads it once right after calling `moveCursor()`) resolves it back to a row
  index so the view can scroll it into view.

`pruneSelection()` (called from `setEntries()`, i.e. every navigation/search/re-sort) drops handles
no longer present, but the anchor and cursor prune on different conditions: the anchor drops when
it's no longer in the *selected set*, while the cursor drops only when its *row* no longer exists —
so a Ctrl-click that toggles the last selected row off leaves the cursor sitting on that row with an
empty selection, deliberately surviving rather than resetting.

Click-driven selection (`selectRow`) is handled entirely by one *background* `TapHandler` per view,
reparented onto the view/`contentItem` itself (`parent: tableView` / `parent: gridView`) rather than
left inside the delegate — `TableView`'s own delegate area is only as tall as its content
(`cellAtPosition`, per Qt's docs), so a tap below the last row/tile would otherwise never be seen.
Per-cell `TapHandler`s in the delegates handle `onDoubleTapped` only, never plain taps: `TapHandler`'s
default `gesturePolicy` (`DragThreshold`) only takes a *passive* grab, so a per-cell handler would
still fire alongside the background one and a Ctrl+click would toggle the same row twice, cancelling
itself back out.

Keyboard (`Ctrl+A` / arrow keys) is wired via `Keys.onPressed` on each view's focus root, not a
window-level `Shortcut` — a `Shortcut` would steal `Ctrl+A` from the header search field regardless
of which view has focus. `moveCursor(delta, modifiers)` takes a view-computed signed row offset
(list: ±1 for Up/Down; grid: ±1 for Left/Right, ±columns for Up/Down — grid geometry is QML-only, so
the view does that arithmetic, not the model) and clamps, never wraps, to `[0, rowCount()-1]`;
Ctrl+arrow is deliberately treated identically to a plain arrow, no distinct "move without
selecting" mode attempted. Because an invisible `StackLayout` child can't hold `activeFocus`, both
views hand focus back explicitly on `StackLayout` view-switch and on click; toolbar buttons get
`focusPolicy: Qt.NoFocus` so clicking one doesn't strand focus away from the active view.

Right-click now selects its target row/tile first — replacing any prior selection, ignoring
Ctrl/Shift, matching Explorer — before opening the context menu, *unless* the target is already part
of the current selection, in which case that selection is left untouched (needed so a future
multi-select bulk action in phase 13 can be invoked via right-click on any already-selected item).

New `tests/FileListModelTest.cpp` (14 cases): row-range clamping, anchor/cursor separation, Shift
range growth/shrink from a fixed anchor, Ctrl-equals-plain-arrow equivalence, `selectAll` semantics,
and cursor survival across a re-sort vs. across pruning on navigation.

**Gotcha (retroactive fix)**: `HorizontalHeaderView`'s `textRole`, set to `""` in phase 6b to silence
a "role doesn't exist" warning, turned out not to work — Qt's existence check treats an empty string
as still-unset and falls back to `"display"` internally regardless, which `FileListModel::roleNames()`
never provided, so the warning kept firing. Fixed by pointing `textRole` at an actual existing role
(`"name"`, never read by the header delegate — it builds its text from `columnLabels` instead) to
satisfy the check. Phase 6b's own gotcha note has been corrected in place to match.

## Phase 8 — breadcrumb trail (done)

Added a Windows-Explorer-style breadcrumb between the `← Back` button and the search field, 7:3
width ratio, left-side (root-first) truncation when it doesn't fit.

Key design decision: `FolderNavigationService::mBackStack` is a *history* stack, not an ancestor
chain — opening a deep folder from search results (which `FolderNavigationController` explicitly
allows) makes the two diverge, and phases 9–11 (tabs/folder tree/quick access) will only widen the
gap. So the breadcrumb path is resolved fresh from the SDK's own node tree (`MegaApi::
getParentNode(MegaNode*)`, walked to the root, purely in-memory once `fetchNodes` has run) rather
than read off the back-stack.

New `src/core/PathSegment.h` (`{name, handle, isRoot}`, `isRoot` following `Location`'s root-sentinel
convention, plus a field-by-field `operator==` for gmock/change-detection — same rationale as
`FileEntry`'s own). `IMegaClient::getPath(handle, isRoot, onDone)` added; `MegaSdkClient`'s
implementation reuses the existing private `resolveNode(handle, isRoot)` helper, walks
`mApi->getParentNode(node.get())` until null (each hop wrapped in `unique_ptr`, ownership
transferred), reverses the collected chain, and normalizes the first element to the sentinel form
(`isRoot = true, handle = 0`) — a root node's own parent is already null on the first hop
(`megaapi.h`'s doc comment on `getParentNode`), so no extra root-only special case was needed.

**Gotcha**: `getParentNode` is a `MegaApi` method, not a `MegaNode` one (`node->getParentNode()`
doesn't compile, `MegaNode` only has `getParentHandle()`) — caught by the `/W4` build (`C2039`), not
by planning against the header first.

`FolderNavigationService` gained two entry points: `navigateTo(handle, isRoot, order, onDone)`
generalizes `openFolder` to also cover the root — needed because a breadcrumb click on the root
segment must push history (Explorer semantics: Back returns to where you were), which the existing
`openRoot` deliberately never does (it's the post-login "home" load). `openFolder` now delegates to
`navigateTo(handle, false, ...)` in one line. `resolveCurrentPath(onDone)` is a thin wrapper over
`mClient->getPath(mCurrent.handle, mCurrent.isRoot, onDone)` — read-only, never touches
`mBackStack`/`mCurrent`.

`FolderNavigationController` gained a `breadcrumb` `Q_PROPERTY` (`QVariantList` of
`{name, handle, isRoot}` — C++ passes structured fields only, QML composes the root's "Cloud Drive"
label, same convention as `NotificationController`/`ErrorToast.qml`) and `Q_INVOKABLE
navigateTo(handle, isRoot)`. `refreshBreadcrumb()` runs at the end of `applyResult`'s success path
— the funnel shared by `loadRoot`/`openFolder`/`goBack`/`navigateTo`/`refreshCurrent` — so the
breadcrumb always reflects the actual current folder rather than navigation history, and keeps
showing the open folder's path (unchanged) while a search is active, since `search()` runs through
`applySearchResult`, not `applyResult`. Because a `QVariantList`-backed `Repeater` has no diffing
and rebuilds every delegate on each emit, `refreshBreadcrumb` builds the new list and skips the
assignment/`breadcrumbChanged()` emit entirely when it's equal to the cached one — otherwise
`refreshCurrentFolder()` (e.g. a sort-order change, same folder) would cause a visible flicker for
no reason. A `getPath` failure is logged via `lcNavigation` and otherwise ignored (no
`NotificationController` toast) — the folder listing itself already succeeded, so a stale breadcrumb
isn't worth an error toast, matching phase 6/7b's "degrade quietly" precedent. `reset()` clears the
breadcrumb and emits the change signal, so a re-login never briefly shows the previous account's
path.

`qml/components/Breadcrumb.qml` (new): each `Repeater` delegate is a labeled segment plus its own
independent ">" separator element — deliberately not merged into the label — so a later phase can
attach a `TapHandler`/`Menu` to just the separator for an Explorer-style "list this folder's
subfolders" dropdown without restructuring the delegate (out of scope for this phase, along with
direct path editing). Left-side overflow uses a two-pass width computation reading each delegate's
`implicitWidth` directly (stable regardless of `visible`) rather than iteratively toggling `visible`
and re-reading the `Row`'s own `implicitWidth`, which doesn't converge in one pass; recomputed via
`Qt.callLater` on width/model/item-count changes. `Main.qml`'s header `RowLayout` gives the
breadcrumb and the search `TextField` `Layout.preferredWidth: 7`/`3` with `Layout.fillWidth: true`
on both and `Layout.minimumWidth: 0` explicit on both (Qt Quick Layouts distributes space between
`fillWidth` items in the ratio of their preferred sizes, confirmed against the Qt 6.11 `Layout` docs)
— giving the exact 7:3 split with no minimum width, as required.

`tests/MockMegaClient.h` gained a `getPath` `MOCK_METHOD` (required — `IMegaClient` is pure virtual).
Six new cases in `tests/FolderNavigationServiceTest.cpp`: `navigateTo`'s root/non-root push-history
behavior and its failure-leaves-state-unchanged case (mirroring the existing `openFolder` tests),
plus `resolveCurrentPath` at the root sentinel, after `openFolder`, and after a `goBack` restores an
earlier location (proving the path comes from the live handle, not a stale "last opened" one).

## Phase 13b — multi-select context menu + action resolution (done)

Pulled forward out of order, same rationale as phase 13a: bulk download doesn't need phase 12's
single-item rename/delete/move first, it just reuses `DownloadService`'s existing queue, so it
didn't need to wait. What was actually built is a declarative resolver, not a one-off "download
button": `src/core/FileAction.h` defines `ActionTarget` (`Any`/`FilesOnly`/`FoldersOnly`) x
`ActionArity` (`Any`/`SingleOnly`/`MultiOnly`) as two orthogonal axes, and `FileActionSpec` pairs a
`FileAction` with one point on that grid. `FileActionResolver.{h,cpp}` (Qt-free, in
`MegaExplorerCore`) resolves a `SelectionSummary` (`{fileCount, folderCount}`) against a
`std::vector<FileActionSpec>` table — `defaultFileActions()` currently holds a single row,
`{Download, FilesOnly, Any}`. Adding rename (`SingleOnly`), delete (`Any`/`Any`), or "open in new
tab" (`FoldersOnly`/`SingleOnly`) once phase 12 lands is a one-line table addition, no resolver
change. `fileActionApplies` rejects every spec outright for an empty selection (so `{Any, Any}`
can't leak an action through when nothing is selected) before checking target/arity.

**C++14 constraint**: confirmed via `docs/BUILD.md`/`CLAUDE.md` that `MegaExplorerCore` compiles at
MSVC's default (C++14) — no `CMAKE_CXX_STANDARD` reaches it since it links no Qt. `FileActionResolver.h`
therefore takes `const std::vector<FileActionSpec>&` and returns `const char*` rather than
`std::span`/`std::string_view`, matching `Result.h`/`FileEntry.h`'s existing workarounds. Verified by
the fact that the build didn't fail on this — a `std::span` mistake would have.

`FileListModel` stays a thin delegator: `selectionSummary()` (typed accessor, counts by walking
`mEntries`+`mSelectedHandles`) feeds `resolveFileActions()`; the new `availableActions`
`Q_PROPERTY` (`QStringList`, sharing `selectionChanged`'s `NOTIFY` with `selectedHandles`) maps the
resolved `FileAction`s through `fileActionId()` to stable string IDs ("download") for QML — a
`Q_ENUM` wasn't used since `FileAction` lives in Qt-free `src/core`, and `Q_ENUM` would need a
`Q_NAMESPACE` wrapper in `src/qml` just for this, more machinery than this codebase's
context-property controllers otherwise carry. New `selectedEntries()` (`Q_INVOKABLE`) returns
`{handle, name, sizeBytes, isFolder}` maps in **row order**, walking `mEntries` rather than
`mSelectedHandles` — the existing `selectedHandleSet()`/`selectedHandlesVariant()` are backed by an
`unordered_set` with no defined order, which is fine for membership checks but wrong for "download
these in a sensible sequence". Neither new accessor caches: `notifySelectionChanged()` already
emits a full-table `dataChanged()` on every selection change (same cost order), and
`pruneSelection()` has a code path (only when the selection actually shrinks) that emits
`selectionChanged()` directly rather than through `notifySelectionChanged()` — an easy spot to leak
a stale cache through, so on-demand computation was simpler than keeping a cache in sync with two
emission paths.

`qml/components/FileContextMenu.qml` rewritten from a `delegateItem`-scoped, `isFolder`-only menu to
a selection-driven one: it now reads `controller.fileListModel.availableActions` and has no
knowledge of which item was right-clicked (safe because both right-click handlers already call
`selectRow()` before `popup()` when the clicked row isn't already selected). Rows are generated with
Qt's documented `Instantiator` + `insertItem`/`removeItem` pattern rather than a `Repeater` — `Menu`'s
`contentItem` isn't a plain `Item` container a `Repeater` can target. This also fixes a real visual
bug, not just a code-quality one: the old menu always instantiated both `MenuItem`s and hid the
inapplicable one via `visible: false` + `height: 0`, but FluentWinUI3's `Menu.contentItem` is a
`ListView` with `spacing: 4` — a zero-height item still consumes its 4px of spacing, so every
folder right-click showed a slightly-too-tall menu with a blank gap above "None". An `Instantiator`
item that was never created reserves no spacing at all. Zero applicable actions (mixed or
folders-only selection) still shows one disabled "None" row rather than an unopenable empty
menu — driven by feeding `[""]` through when `availableActions` is empty; the same fallback also
catches a future action ID this file's `actionLabels` map hasn't been updated for yet (`label`
is deliberately `property var`, not `property string` — a string-typed property coerces a missing
map lookup to `""` instead of preserving `undefined`, which would have silently defeated the
`enabled: label !== undefined` check).

Both call sites (`qml/views/FileTableView.qml`, `qml/Main.qml`) moved their `FileContextMenu` out of
the per-cell/per-tile delegate and up to one instance per view (`ColumnLayout`/`GridView` level,
alongside `SystemPalette`) — the old placement meant one live `Menu` per delegate instance (up to
3000 for a 1000-row x 3-column `TableView`). `Menu` is a `Popup`, not an `Item`: it isn't laid out
by `ColumnLayout` and isn't clipped by `TableView`/`GridView`'s `Flickable` viewport, and a
parentless `popup()` opens at the mouse cursor regardless of where the `Menu` object lives in the
tree — so this move needed no changes to either right-click `TapHandler`, which stayed in the
delegates.

`DownloadService` gained `hasJobForHandle(handle)` (mutex-guarded linear scan, no copy) alongside
the existing `jobs()` (which copies the whole queue). `DownloadController::downloadFile`'s
already-queued guard now calls it instead of copying `jobs()` into a range-for — `jobs()` was
already fine for a single download, but the new bulk-download path calls `downloadFile()` once per
selected file, which made the old copy-per-call O(N^2) over the selection size.

New `tests/FileActionResolverTest.cpp` (21 cases, Qt-free): full 3x3 `ActionTarget`x`ActionArity`
grid rejecting an empty selection, each target/arity value in isolation, a combined-constraint case,
`resolveFileActions`'s order-preservation and empty-selection short circuit, the real
`defaultFileActions()` table's behavior across single/multiple-file/folder/mixed/empty selections,
and `fileActionId(Download) == "download"` pinned as a stability regression test (it's the only
contract linking the C++ enum to the QML `actionLabels` map's keys). New cases in
`tests/FileListModelTest.cpp` (8): `selectionSummary()` counting, `availableActions()` across
file/folder/mixed/empty/post-navigation selections, and `selectedEntries()` returning row order
(not insertion order) with correct name/size/isFolder fields. New cases in
`tests/DownloadServiceTest.cpp` (2): `hasJobForHandle` true for both an active and a queued job,
false for an unrelated handle.

**Scope note**: progress UI for a bulk download is unchanged — `DownloadSnackbar` still reuses a
single `Popup` for whichever job is currently active, so a fast sequence of small downloads can
flash through without every individual failure/already-present notice being visible. The queue
itself is correct (each job still runs and finishes), just not all individually surfaced;
aggregate/count progress display is left for a later phase.
