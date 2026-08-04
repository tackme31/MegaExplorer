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
| 9 | Windows-Explorer-style tabs (multiple folder views) | done |
| 10 | Folder tree navigation (side panel) | done |
| 11 | Quick access (pinned folders, side panel) | done |
| 12 | Rename / delete (move to rubbish) | done (move deferred, see below) |
| 13a | Selection model (row/cell, keyboard, right-click) | done (pulled forward) |
| 13b | Multi-select context menu + action resolution | done (pulled forward) |
| 13 | Multi-select + bulk operations | done (bulk move delivered by 14a) |
| 14a | Move via drag & drop | done |
| 14b | Upload (drag & drop) | done |
| 17 | Title-bar-integrated tabs (Windows, QWindowKit) | done (pulled forward) |
| 18 | Login loading screen + SDK cache location | planned (next up) |
| 15 | In-app preview (side panel, `getPreview`) | planned |
| 16 | Real-time remote-change reflection | future, post-MVP |
| 19+ | Undecided | full bidirectional local sync stays out of scope |

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

Tab strip above the breadcrumb (phase 8), each tab its own independent navigation context
(back-stack, current folder, search state, sort order, view mode) rather than one shared
`FolderNavigationService` instance. Placed right after breadcrumb and before the folder-tree side
panel (phase 10) and quick access (phase 11) deliberately: those two become shared chrome sitting
beside N tabbed content panes, cheaper to design that way from the start than retrofitting
tab-awareness onto a side panel that was built assuming a single pane. Persisting the open tab set
across restarts remains a stretch goal, not required for the phase to be done (a fresh launch always
starts with one tab at the root). See the Phase 9 implementation-log entry below for what was built.

### Phase 10 — folder tree navigation (side panel)

New left side panel; grouped with breadcrumb as "where am I / where can I go" nav, and becomes the
home for quick access (phase 11). Expected to reuse `IMegaClient::getChildren` for lazy expansion;
check performance on large folders. Shared across tabs (phase 9), not duplicated per tab.

### Phase 11 — quick access (pinned folders, side panel)

Adds a pinned-folders section to phase 10's panel. Persist via `Settings` (QtCore). Dangling-pin
handling (target deleted/moved) should be designed together with phase 12, not bolted on after. See
the Phase 11 implementation-log entry below for what was built — in particular, "moved" turned out
not to need any handling at all (a MEGA handle is stable across moves *and* renames), so only
deletion breaks a pin.

### Phase 11a — per-account quick-access scoping

Unplanned correction of Phase 11's "Known limitations" entry: pins were persisted under one
machine-wide QSettings key, so logging into a different MEGA account didn't just hide the previous
account's pins, it silently dropped them via the login-time validation sweep (every handle fails to
resolve against the new account, so the sweep treats them all as dangling and overwrites the shared
key with the survivor list). See the Phase 11a implementation-log entry below.

### Phase 12 — rename / delete (move to rubbish)

Done — see its implementation-log entry. Move (`moveNode` to an arbitrary parent) was cut from the
phase and deferred: with no destination picker in the UI, the only sane trigger for it is
drag & drop, which belongs with phase 14's `DropArea` work rather than here. **Phase 14a has since
delivered it.** The Rubbish-bin snackbar still has no Undo button, though: a general move now exists
to undo *with*, but nothing records each item's pre-move parent to undo *to*.

### Phase 13 — multi-select + bulk operations

Sequenced after phase 12 (bulk ops need single-item versions first). Needs a selection model for
both `TableView` and `GridView` — built ahead of schedule as phase 13a (see below and its
implementation-log entry). The selection-driven context menu plus a declarative
target/arity action-resolution table were also pulled forward as phase 13b (see its
implementation-log entry) since bulk download doesn't actually depend on phase 12 — it reuses
`DownloadService`'s existing queue directly. Remaining scope: rename/delete/move as multi-item
context-menu actions. Phase 12 has since delivered rename (single-only by construction) and
multi-item move-to-rubbish, both purely as new `FileActionSpec` rows exactly as predicted here — so
what was actually left for 13 was bulk *move* — **delivered by phase 14a**, which makes multi-select
drag & drop the trigger. Nothing remains in phase 13.

### Phase 14a — move via drag & drop (done)

Phase 12's deferred `moveNode`, triggered by dropping onto a folder row, a view's empty space, a
folder-tree row, a quick-access pin, or a breadcrumb segment. See its implementation-log entry.
Split out of phase 14 so the `DropArea` groundwork could land without waiting on upload's transfer
listener.

### Phase 14b — upload (drag & drop) (done)

Drop files from Explorer onto any of 14a's five drop targets. See its implementation-log entry.

### Phase 17 — title-bar-integrated tabs (done, pulled forward)

Run ahead of 15/16 — same "self-contained, doesn't need the phases before it" reasoning that pulled
13a/13b/14a forward. It touches only window chrome (QML plus one CMake dependency), shares nothing
with preview or remote-change reflection, and the investigation memo it implements
(`docs/TITLEBAR_TABS_INVESTIGATION.md`) was already written and decided. Split into 17a (frameless
window + own caption row, tabs left where they were) and 17b (tab strip moved onto that caption
row), so the shared cost landed and stabilised before the risky part. See both implementation-log
entries.

### Phase 18 — login loading screen + SDK cache location

Ahead of 15/16: the login screen currently shows **nothing** between submitting the form and the
Cloud Drive appearing (`LoginView.qml` gates its indicator on `authState === Restoring` only), and
on a large account that gap was **measured at 6 minutes 25 seconds**. Self-contained — auth path
plus one composition-root line, shares nothing with preview or remote-change reflection.

Design, measurements and the reasoning behind every decision below live in
`docs/FETCHNODES_PROGRESS_INVESTIGATION.md` (read 追記2 first — it supersedes the earlier
predictions). Do not re-derive them here; this is only the checklist.

- [ ] **`basePath` を `AppLocalDataLocation` に固定** (`main.cpp:63` / `MegaSdkClient.h:21`, now
      `"."` = the launch CWD). Independent of everything below and the single biggest win: with the
      SDK state-cache DB found, the same account's fetchNodes was **619 ms instead of 384.8 s**.
      A changing CWD silently costs a full re-fetch.
- [ ] **`IMegaClient::fetchNodes` に `onProgress(transferred, total)` を追加** — same two-callback
      shape `download`/`upload` already use. Fakes/mocks under `tests/` follow.
- [ ] **`AuthService` の 3 メソッド**（`restoreSession`/`login`/`loginWithTwoFactor`）が進捗を
      素通しする。
- [ ] **`AuthController` に読み込み段階の状態を持たせる** — stage enum（認証 / 準備 /
      ダウンロード / 復号）、受信・総バイト数、経過秒。SDK スレッドからの値は
      `DownloadController.cpp:26` と同じ `QMetaObject::invokeMethod(qApp, …, Qt::QueuedConnection)`
      で GUI スレッドへ。
- [ ] **「ダウンロード完了」判定に無更新タイムアウトのフォールバック**を入れる（実測でログに
      残った最後の値は 99.44%。100% ちょうどが観測できる保証がない）。
- [ ] **`LoginView.qml` をローディング対応にする** — 現在 `Restoring` にしか出していないので
      `LoggingIn`/`VerifyingTwoFactor` でも出す。ダウンロード中のみ確定バー、復号中は不定 +
      経過時間 + 「初回のみ数分かかります」。
- [ ] ついでに **S11 の「フォームとインジケータが同じ `ColumnLayout` にあって高さが跳ねる」**
      （`docs/DESIGN_IMPROVEMENT.md` 8 節）をここで `StackLayout` にして解消する。

やらないと決めたもの: 全体を 1 本の % で見せること（ダウンロードは全体の 42% でしかなく、残り
57% は進捗を出す手段が SDK に無い）、ノード件数の表示（`getNumNodes()` はルート確定まで 0）、
`EVENT_REQSTAT_PROGRESS`（`reqstat` リクエスト自体が失敗しており実質使えない）。

別件として拾ったもの（このフェーズの範囲外）: `MegaApi::logout` は状態キャッシュ DB を破棄する
ので、**サインアウト 1 回のコストが 6 分半**になる。導線に警告を出すかどうかは未決。

### Phase 15 — in-app preview (side panel, `getPreview`/`startStreaming`)

Lowest priority; explicitly deferred in Phase 4 since download→open already covers "view the file".
Format-specific rendering (image/PDF/text/...) makes this the highest-effort item on the list.

### Phase 16 (future) — real-time remote-change reflection

Reflect other devices' changes via the SDK's push-notification mechanism into whatever listing is
open. Additive on top of phase 6's refresh.

### Phase 18+ — undecided

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

## Phase 9 — Windows-Explorer-style tabs (done)

Replaced the single app-lifetime `FolderNavigationController`/`ThumbnailController` composition-root
instances with N per-tab instances managed by a new `src/qml/TabsController` (`QAbstractListModel`).
`TabsController` doubles as both `TabStrip.qml`'s model and the app's tab-management command surface
(`addTab()`/`addTabAt(handle, isRoot)`/`closeTab(index)`/`loadRootAll()`/`resetAll()`,
`currentIndex`/`currentNavigation`/`count` properties, `lastTabClosed` signal) — a `QAbstractListModel`
was chosen over a `QVariantList` (the `Breadcrumb.qml` precedent) specifically because a
`QVariantList` has no row-level diffing and would rebuild every delegate on each emit, which is fatal
for tabs: the entire point is that each pane's scroll position/selection/focus survives a tab switch,
which needs `beginInsertRows`/`beginRemoveRows` so `Main.qml`'s `Repeater` only adds/removes the panes
that actually changed.

Each tab is a `TabContext` (`src/qml/TabsController.h`): its own `FolderNavigationService` +
`SearchService` + `FolderNavigationController` + `ThumbnailController`, built by a factory
(`std::function<TabContext()>`) that `main.cpp`'s composition root hands to `TabsController`'s
constructor — tabs are created dynamically, unlike every other controller `main.cpp` can construct
once up front, but only `main.cpp` is allowed to know the wiring (`docs/ARCHITECTURE.md`'s
composition-root convention), so `TabsController` never constructs a `FolderNavigationService` etc.
itself. `ThumbnailService`/`DownloadService`/`AuthService`/`NotificationController` all stay
app-lifetime singletons shared across every tab — only the pieces that are inherently
per-navigation-scope (back-stack, current folder, last search query, and `ThumbnailController`'s
handle to that tab's own `FileListModel`) are duplicated per tab.

**Lifetime — the phase's sharpest edge**: `FolderNavigationController`/`ThumbnailController` had
always lived for the app's whole lifetime before this phase, so their SDK-thread async callbacks
capturing `this` were always safe. Once a tab (and its controllers) can be destroyed mid-fetch by
closing it, that stopped being true. Fixed by making both classes inherit
`std::enable_shared_from_this`, having every async callback lambda capture a `self = shared_from_this()`
copy (keeping the controller alive for that callback's duration), and changing the existing
`invokeOnGuiThread` helper's queued-invoke target from `qApp` to `this` — `QObject`'s destructor
removes any of its own still-queued posted events, so a controller destroyed after the post but before
the GUI thread processes it now just drops the stale event instead of running against a dangling
pointer, closing the second half of the race the `self` capture alone doesn't cover. `TabContext`'s
`navigation`/`thumbnails` are deliberately `shared_ptr`, not raw pointers with a `QObject` parent, so
this reference-counted lifetime holds; `TabsController::createTab()` calls
`QQmlEngine::setObjectOwnership(..., QQmlEngine::CppOwnership)` on both before ever handing them to
QML, since a parentless `QObject*` handed across the engine boundary would otherwise default to
`JavaScriptOwnership` and the QML GC could delete a controller out from under the `shared_ptr`s still
holding it.

**Tab titles**: `FolderNavigationController` gained `currentFolderName`/`atRoot` `Q_PROPERTY`s,
both derived from `mBreadcrumb`'s last element and sharing its `breadcrumbChanged` `NOTIFY` — same
"C++ passes structured fields, QML composes the string" split as `NotificationController`/
`ErrorToast.qml`. `TabsController::createTab()` connects each tab's `breadcrumbChanged` to a lambda
that looks up that tab's *current* row by pointer (not a captured index, which insert/remove would
make stale) and emits a per-row `dataChanged({TitleRole, AtRootRole})`, so `TabStrip.qml`'s
`TabButton` delegates update without their own `Connections`.

**`FileAction::OpenInNewTab`**: one enum value + one `defaultFileActions()` row
(`{OpenInNewTab, FoldersOnly, SingleOnly}`), no `FileActionResolver` changes needed — the enum
comment already flagged this as a Phase 13b future case. Wired into `FileContextMenu.qml`'s
`actionLabels` map and `onTriggered` (`tabsController.addTabAt(entries[0].handle, false)`).

**QML**: `qml/views/FileGridView.qml` is the old inline `GridView` from `Main.qml` (~170 lines)
extracted verbatim into its own file, matching `FileTableView.qml`'s existing shape
(`navController`/`thumbController` required properties, `activateRequested`/`openInNewTabRequested`
signals in place of the old `controller`/`thumbnailController`/`window.activateEntry()` references).
Both grid and table delegates gained a `TapHandler { acceptedButtons: Qt.MiddleButton }` that emits
`openInNewTabRequested` for folder rows only (files have nothing sensible to open in a new tab, same
restriction as the context-menu action). `qml/views/TabContentPane.qml` is one tab's content: a
`StackLayout` between `FileTableView`/`FileGridView` plus that tab's own `viewMode` and (via
`FileTableView`'s `initialSortColumn`/etc.) sort order/column widths. `qml/components/TabStrip.qml` is
a `TabBar` + `Repeater` over `tabsController` + a trailing "+" `ToolButton`; each `TabButton` sets
`checkable: false` and drives its own `checked`/`onClicked` explicitly rather than relying on
`TabBar`'s built-in click-driven exclusive-group bookkeeping — `TabBar` normally reacts to a
`TabButton.checked` becoming `true` by writing its own `currentIndex`, and any such external write
(from anywhere, QML or C++) permanently tears off a previously-declared QML binding on that property;
since `tabsController.currentIndex` has to stay the single source of truth (`Main.qml`'s central
`StackLayout.currentIndex` is bound to it too), letting `TabBar` fight over ownership of its own
`currentIndex` would have desynced the two. `checkable: false` sidesteps the whole problem by never
letting `TabBar`'s internal mechanism touch it.

**Settings, centralized and made last-write-wins**: previously `FileTableView.qml` owned its own
`Settings` item for sort column/column widths, and `Main.qml` owned one for `viewMode` — both assumed
exactly one live view instance. With N tabs each running their own `FileTableView`, N `Settings` items
would fight over the same registry keys. Fixed by moving `viewMode`/`sortColumn`/`sortAscending`/
`columnWidthName`/`columnWidthModified`/`columnWidthSize` onto `window` in `Main.qml` as the single
`Settings`-backed copy, and threading them through explicitly: `Main.qml`'s `Repeater` delegate passes
`window.*` in as `TabContentPane`'s `initial*` required properties (a brand-new tab's starting point);
`TabContentPane`/`FileTableView` copy those into their own plain properties exactly once, imperatively,
in `Component.onCompleted` — deliberately *not* a live QML binding (`property int viewMode:
initialViewMode` would keep tracking `initialViewMode`/`window.viewMode` forever), since a live
binding would mean every already-open, untouched tab keeps jumping to match whichever tab wrote last,
which is the opposite of "only *new* tabs pick up the latest value". Each tab's own subsequent changes
fire `viewModeWriteBack`/`sortOrderWriteBack`/`columnWidthsWriteBack` signals that bubble
`FileTableView` → `TabContentPane` → `Main.qml`'s `Repeater` delegate, which writes them into
`window.*` (and so into `Settings`) immediately — this is the "last write wins" half of the spec.
Neither `TabContentPane.qml` nor `FileTableView.qml` can reference `window` by id directly since
they're separately-loaded QML files (documented precedent already existed in `FileTableView.qml` for
why `activateRequested` has to be a signal rather than a direct `window.activateEntry()` call), hence
the explicit required-property/signal plumbing instead of relying on that id.

The footer's `☰`/`⊞` toggle now reads/writes `window.currentPane.viewMode` (the *active* tab's own view
mode) rather than a single `window.viewMode`. `window.currentPane` is kept in sync by a `Binding`
element inside `mainContentComponent` (`value: paneRepeater.itemAt(tabsController.currentIndex)`,
re-evaluated whenever `tabsController.currentIndex`/`paneRepeater.count` change) — needed because
`footerComponent` is a sibling nested `Component`, so it can't see `mainContentComponent`'s internal
`paneRepeater` id, only `window`'s own properties (same file-vs-nested-`Component` id-scoping rule as
above, just at the `Component`-sibling granularity instead of the separate-file granularity).

**Tests**: `tests/FileActionResolverTest.cpp` gained `OpenInNewTab` cases (`fileActionId` stability,
`defaultFileActions()` offering it only for a single-folder selection, never for
multiple-folders/single-file/mixed). New `tests/TabsControllerTest.cpp` (12 cases) is a deliberate,
narrow exception to "`src/qml` is GUI glue, untested by convention" — same rationale as
`FileListModelTest.cpp`: `TabsController`'s row/`currentIndex` bookkeeping is pure and genuinely
bug-prone (the `closeTab` clamping arithmetic in particular), unlike view/rendering glue. Covers
`addTab`/`addTabAt` row-count and `currentIndex` bookkeeping, `closeTab`'s three
index-relative-to-the-closed-tab cases (before/at/after the active tab) including the
last-tab-closed → `lastTabClosed` signal path, and `loadRootAll`/`resetAll` collapsing back to one
tab. Builds real controllers against `MockMegaClient` (no `EXPECT_CALL`s set up — the resulting
"uninteresting mock call" `GMOCK WARNING`s on stderr are expected and harmless) rather than mocking
`TabsController`'s own dependencies, since the point is exercising the real bookkeeping, not the
network calls; no assertion depends on an async fetch actually completing.
`tests/CMakeLists.txt` needed `Qt6::Qml` added (previously just `Qt6::Core` sufficed) solely for
`TabsController.cpp`'s `QQmlEngine::setObjectOwnership` call — resolved via the same
`find_package(Qt6 COMPONENTS Quick)` the root `CMakeLists.txt` already runs, no separate
`find_package` needed in the test target.

**Build gotcha hit during this phase**: after adding the three new QML files to `qt_add_qml_module`'s
`QML_FILES` in `CMakeLists.txt`, an incremental `cmake --build` (without re-running `cmake --preset
msvc-debug` first) failed at link time with unresolved `QmlCacheGeneratedCode::...::qmlData`/
`aotBuiltFunctions` symbols for every new file (and, oddly, for the pre-existing `LoginView.qml` too) —
the AOT-compiled `qmlcache_loader.cpp` aggregator was stale relative to the newly-listed `QML_FILES`.
Re-running the configure step first fixed it. Worth remembering for any future phase that adds new
`.qml` files: reconfigure, don't just rebuild.

## Phase 10 — folder tree navigation (side panel) (done)

New left `SplitView` panel next to the tab content, shared across every tab (an app-lifetime
singleton, not duplicated per tab like `FolderNavigationService`/`FolderNavigationController`) —
placed right after tabs (phase 9) deliberately, per that phase's own log entry, so this panel could
be designed from the start as shared chrome beside N tabbed panes.

Three new layers mirror the existing `IMegaClient` → `*Service` → `*Controller`/`*Model` shape:
`src/core/FolderTreeService.{h,cpp}` (new, `MegaExplorerCore`) resolves the `isRoot`/`getRootChildren`
vs. `getChildren` split `IMegaClient` exposes (unlike `getPath`/`search`, `getChildren` has no `isRoot`
overload), filters results down to folders only, and always requests `SortOrder{Name, ascending}` —
independent of whatever sort order any tab's file-list view currently has, since the tree is a
navigation aid, not a sortable listing. `src/qml/FolderTreeModel.{h,cpp}` (new) is a
`QAbstractItemModel` owning a lazily-expanded `TreeNode` tree: `children` is a
`vector<unique_ptr<TreeNode>>`, not `vector<TreeNode>`, because `QModelIndex::internalPointer` stores
a raw `TreeNode*` that must never be invalidated by a sibling insertion reallocating the vector. An
invisible sentinel root owns a single always-present "Cloud Drive" node (`isRoot = true`), so the tree
shows exactly one visible top-level row that can itself be clicked to navigate to the root — and
becomes the natural place to add phase 11's pinned folders later. `Qt::DisplayRole` returns the node's
`name` directly (including the literal, hardcoded `"Cloud Drive"` string for that one sentinel node) —
a deliberate, narrow exception to this codebase's usual "C++ passes structured fields, QML composes
text" split (`Breadcrumb.qml`/`TabStrip.qml` compose that same label in QML instead), made here because
`FolderTreePanel.qml` uses `TreeViewDelegate`'s default `contentItem` unmodified, which reads
`Qt::DisplayRole` with no isRoot-aware composition step of its own.

**Lazy expansion**: `hasChildren()` returns `true` for any not-yet-`Loaded` node (so the expand arrow
shows before the first fetch) and falls back to the real child count once `Loaded`; `canFetchMore()`
is `true` only while `NotLoaded`; `fetchMore()` delegates to `ensureLoaded()`. That
`canFetchMore`/`fetchMore` path is the *only* fetch trigger, and it is enough on its own — an earlier
attempt also called `ensureLoaded()` from a QML-side `TreeView.onExpanded` handler as
belt-and-suspenders, on the theory that Qt's `expandRecursively()` doc note ("this function will not
try to fetch more data") implied plain `expand()` might not fetch either; expanding an unfetched node
was measured against Qt 6.11 and does go through `fetchMore()`, so the redundant QML call (and
`ensureLoaded()`'s `Q_INVOKABLE`) were dropped. A successful load with one or more children
does a proper `beginInsertRows`/`endInsertRows`; a successful load with *zero* children can't use that
(`beginInsertRows` with an empty range is invalid) so it just flips the state and emits `dataChanged()`
— **known limitation**: `TreeView`'s internal proxy isn't guaranteed to re-query `hasChildren()` off a
plain `dataChanged()`, so an empty folder's expand arrow can linger with nothing to show on click;
matches (English) Explorer's own long-standing behavior here, accepted rather than worked around. A
failed load resets the node back to `NotLoaded` (not stuck in `Loading`) so re-expanding retries, logs
via the existing `lcNavigation` category (no new category added), and never raises a
`NotificationController` toast — same "degrade quietly, the primary listing is unaffected" precedent as
phase 8's breadcrumb-resolution failures. Unlike `FolderNavigationController`/`ThumbnailController`
(phase 9), `FolderTreeModel` does **not** use `enable_shared_from_this`: those two needed it because a
*tab* (and its controllers) can be destroyed mid-fetch by closing it, but this model is an app-lifetime
singleton with no such per-tab destruction race, so its `invokeOnGuiThread` targets `qApp` directly
(same reasoning, and the same target choice, as the pre-existing `DownloadController`). The *nodes*,
though, are not app-lifetime: `reset()`/`reload()` rebuild the whole `TreeNode` tree, which would leave
an in-flight load's captured raw `TreeNode*` dangling, so each load captures a `mGeneration` counter
(bumped by every `resetTree()`) and drops its result if the tree has been rebuilt since.

**Highlight, not auto-expand/auto-scroll** (confirmed with the user before implementation):
`FolderNavigationController` gained a `currentHandle` `Q_PROPERTY` (`quint64`, `NOTIFY
breadcrumbChanged`), derived from `mBreadcrumb`'s last element exactly like the pre-existing
`currentFolderName`/`atRoot`. `FolderTreePanel.qml`'s delegate background compares
`(isRoot ? navController.atRoot : (!navController.atRoot && handle === navController.currentHandle))`
to decide whether to paint the translucent highlight (reusing `SystemPalette`/`Qt.rgba(...,0.35)`,
matching `FileTableView.qml`/`FileGridView.qml`'s own selection-highlight color). `navController` is
rebound by `Main.qml` to whichever tab is currently active, so switching tabs moves the highlight
without touching the tree's own expansion state; navigating in the file view (double-click, breadcrumb,
back) moves the highlight without expanding or scrolling the tree to reveal it, exactly as scoped.

**`FolderTreePanel.qml`** (new): Explorer semantics -- the chevron expands/collapses, the label
navigates. This is `TreeViewDelegate`'s *stock* pointer behavior and needs no help: its built-in
handling toggles expansion only for presses that land on the indicator, so a plain `TapHandler` on the
delegate calling `navController.navigateTo(handle, isRoot)` covers the other half, since it only ever
receives the clicks the indicator didn't take. **Trap worth remembering** (cost this phase a full
debugging round): the first implementation read the docs' "you can set `pointerNavigationEnabled` to
false to disable the default behavior" as an instruction to take expansion over manually --
`pointerNavigationEnabled: false` on the `TreeView` plus a custom `indicator:` carrying its own
`TapHandler` calling `toggleExpanded(row)`. The result was a chevron that did nothing at all: the flag
does kill the built-in indicator toggle, but the replacement `TapHandler` never fires either, because
the delegate itself consumes the press before a handler on the indicator child sees it (a `MouseArea`
in the same position *does* receive it -- so the item is hit-tested fine, it's specifically pointer
handlers there that are starved). Replacing `indicator:` is also what forced the manual
`leftMargin + (depth * indentation)` indentation and its attendant `implicitHeight` binding loop.
Verified by rebuilding the panel against a stubbed `IMegaClient` in a throwaway Qt Quick harness and
driving it with `QTest::mouseClick`. No `selectionModel` is set on the `TreeView` -- the highlight
follows navigation state (`isCurrent` above), not a click-driven selection, so one was never needed.
The delegate's `background:` is replaced to paint that highlight, which means restating the style's own
`implicitHeight: 24`; without it, rows whose indicator is hidden (a folder already known to be empty)
render shorter than their siblings.
Middle-click mirrors the file views' existing "open in new tab" convention
(`tabsController.addTabAt(handle, isRoot)`, background tab, current tab keeps focus). Each
`TreeViewDelegate` gets `focusPolicy: Qt.NoFocus`, matching `TabStrip.qml`'s `TabButton` precedent --
without it, clicking a tree row stalls the file view's own arrow-key navigation until it's re-clicked.

**`Main.qml`**: `mainContentComponent`'s root changed from a bare `StackLayout` to a `SplitView`
containing `FolderTreePanel` (`SplitView.minimumWidth: 120`/`maximumWidth: 500`) and the pre-existing
`StackLayout` (`SplitView.fillWidth: true`) -- the `paneRepeater`/`window.currentPane` `Binding` pairing
stayed inside the `StackLayout`, unchanged, per the file's own existing comment on why that pairing must
stay in one `Component` scope. `treePanel.SplitView.preferredWidth` is read once, imperatively, in
`Component.onCompleted` from a new `window.treePanelWidth` property (default `240`) rather than bound
live -- same "one-shot read, not a live binding" convention as `TabContentPane.qml`'s
`initialViewMode`, needed here because `SplitView` itself writes back to the panel's width as the user
drags the splitter (`onResizingChanged: if (!resizing) window.treePanelWidth = treePanel.width`), and a
live binding would fight that write-back. `window.treePanelWidth` is a `Settings`-backed alias
alongside the existing `viewMode`/`sortColumn`/etc. ones, so panel width persists across restarts like
everything else `window` already owns. (A header `☰` `ToolButton` plus a `window.treePanelVisible`
property originally let the panel be hidden; both were removed in a 2026-07-30 follow-up -- see the
note at the end of this phase's log -- since the panel is now always shown.) The `authStateChanged`
`Connections` handler now also calls `folderTreeModel.reload()` (`LoggedIn`, alongside
`tabsController.loadRootAll()`) / `folderTreeModel.reset()` (`LoggedOut`, alongside
`tabsController.resetAll()`), so a sign-out clears the tree back to a single "Cloud Drive" row and a
subsequent login (possibly a different account) starts loading fresh rather than showing stale folders.

**`main.cpp`**: `folderTreeService`/`folderTreeModel` are constructed once, at the same app-lifetime
scope as `thumbnailService`/`notifications` (not inside `tabFactory`, since the panel is shared, not
per-tab), and `folderTreeModel` is exposed as the `folderTreeModel` context property alongside the
existing ones. `TabsController`'s factory lambda/`TabContext`/`Roles` were untouched, as scoped.

**Tests**: `tests/FolderTreeServiceTest.cpp` (5 cases, `MegaExplorerCore`, `MockMegaClient`): root vs.
non-root requests route to `getRootChildren`/`getChildren(handle, ...)` respectively, the fixed
name-ascending `SortOrder` is passed, files are filtered out of a mixed result, and failure propagates.
`tests/FolderTreeModelTest.cpp` (7 cases) is a deliberate, narrow exception to "`src/qml` is GUI glue,
untested by convention" -- same rationale as `FileListModelTest.cpp`/`TabsControllerTest.cpp`:
`index()`/`parent()` round-tripping and load-state bookkeeping are pure and bug-prone, not rendering
glue. Builds a real `FolderTreeService` against `MockMegaClient` (same approach as
`TabsControllerTest`) rather than mocking the service. **Gotcha**: `ensureLoaded()`'s result always
arrives through a queued `invokeMethod` even when `MockMegaClient`'s `InvokeArgument` action fires
synchronously, so the test file owns this binary's single `QCoreApplication` (a function-local static,
not a file-scope global -- it must not be constructed during static initialization, and its `argc`/
`argv` have to outlive it) and calls `QCoreApplication::processEvents()` after triggering a load, to
actually flush the queued callback before asserting on the resulting state -- no existing test in this
codebase needed a `QCoreApplication` before, since every prior `src/qml`-adjacent test
(`TabsControllerTest`) deliberately avoids ever letting its mocked async callbacks fire. The seventh
case pins the generation guard described above by holding the load callback (`SaveArg`), calling
`reset()`, then firing it -- an unguarded model use-after-frees there rather than merely mis-inserting.

**Unrelated fix noticed while running the full suite for this phase**: `tests/TabsControllerTest.cpp`'s
`AddTabAtIncreasesCountAndSwitchesToNewTab` was left stale by the immediately preceding commit ("Open
background tabs without switching focus"), which changed `addTabAt` to leave the current tab focused
but never updated this test's assertion -- renamed to
`AddTabAtIncreasesCountWithoutSwitchingFocus` and corrected to expect `currentIndex() == 0`, matching
the now-documented behavior in `TabsController.h`.

**2026-07-30 follow-up**: removed the panel's show/hide toggle -- the header `☰` `ToolButton` in
`Main.qml`'s `headerComponent` and the `window.treePanelVisible` property (plus its `Settings` alias
and the `visible: window.treePanelVisible` binding on `FolderTreePanel`) are all gone, so the panel is
now always shown, same as the tab strip/breadcrumb bar. `window.treePanelWidth` (drag-to-resize
persistence) is unaffected. No test exercised the toggle directly, so no test changes were needed.

## Phase 11 — quick access (pinned folders, side panel) (done)

Pinned folders in a section above phase 10's folder tree, matching Explorer's placement. Pins are
stored by MEGA node handle, which turned out to settle most of the phase's design questions at once:
a `MegaHandle` is stable for the node's whole lifetime, so **a pin follows both moves and renames for
free** and the only thing that can break it is deletion. The roadmap's "target deleted/moved" framing
was therefore half a non-problem.

**Gotcha that shaped the whole phase — `getPath` can't see the Rubbish bin.** A MEGA delete is really
a move to the Rubbish bin, so a deleted node's handle keeps resolving. Worse,
`MegaSdkClient::getPath` normalizes whatever root its parent walk ends at into the Cloud Drive
sentinel (`isRoot = true, handle = 0`, unchanged from phase 8), so a node sitting in the Rubbish bin
comes back as a perfectly ordinary Cloud-Drive-rooted path. Validating pins through `getPath` would
have reported every deleted folder as alive. Fixed with a new port method rather than by touching
`getPath`'s phase 8 contract: `IMegaClient::getNodeInfo(handle, onDone)` → `Result<NodeInfo>`
(`src/core/NodeInfo.h`: `{name, handle, isFolder, inCloud}`), implemented over the existing private
`resolveNode(handle, false)` plus `MegaApi::isInCloud(node)`. It walks no ancestors, so it's also
cheaper than `getPath` — and phase 12's rename/delete will want the same "what is this handle now"
lookup. **Per the user's decision, "in the Rubbish bin" counts as deleted** (`isInCloud == false`,
which also covers the Vault): the app never shows the Rubbish bin, so a pin that navigated into it
would land the file list somewhere the tree can't represent.

Persistence follows the `ISessionStore`/`WindowsSessionStore` port+adapter shape rather than being
done in QML, even though the roadmap said "persist via `Settings` (QtCore)" — both halves were kept:
`src/core/IPinnedFolderStore.h` (+ `src/core/PinnedFolder.h`, `{name, handle}` with the usual
field-by-field `operator==` for gmock) is the Qt-free port, and `src/platform/
QSettingsPinnedFolderStore.{h,cpp}` is the adapter, writing a single JSON array string to
`QSettings`' `quickAccess/pinnedFolders` key — i.e. the same registry location `Main.qml`'s QML
`Settings` item already uses, which keeps to the roadmap's storage choice while leaving the pin logic
in testable C++. The alternative considered and rejected was a `pinnedFoldersJson` alias on `window`
with the list logic in QML JS: validation needs `IMegaClient`, which only exists in C++, so that
would have split the pin data (QML) from the code that validates it (C++) across two layers with no
unit test possible. Deliberately *not* `QSettings::beginWriteArray`: an array write leaves a `size`
key plus one subgroup per element behind, and shrinking the list needs an explicit `remove()` first
or stale indices survive. A `handle` of 0 is skipped on load (never a real node, so it can only come
from a hand-edited or truncated entry) rather than failing the whole load; a JSON parse failure logs
via a new `lcQuickAccess` category and degrades to an empty list.

`src/core/QuickAccessService.{h,cpp}` (`MegaExplorerCore`) owns the list and writes through to the
store on every mutation: `load`/`pins`/`isPinned`/`pin`/`unpin`/`replaceAll`/`clear`, plus
`resolveFolder` as a thin `getNodeInfo` pass-through and a static `isUsable(Result<NodeInfo>)` that
is the codebase's single definition of "this pin still points at something usable"
(`success && isFolder && inCloud`). Everything except `resolveFolder` is synchronous and runs on the
caller's thread, deliberately: driving the N-way login-time validation sweep is `QuickAccessModel`'s
job, matching the `FolderTreeService`/`FolderTreeModel` split (services pass through, `src/qml`
marshals). Putting the sweep in the service would mean mutating its vector from the SDK thread while
the GUI thread reads it. `clear()` (sign-out) empties memory but deliberately leaves the store
intact so signing back in restores the pins.

`src/qml/QuickAccessModel.{h,cpp}` is a flat `QAbstractListModel` (`NameRole`/`HandleRole`, `count`
`Q_PROPERTY`) — no tree, since a pin is a shortcut and never expandable. Same lifetime shape as
`FolderTreeModel`: an app-lifetime singleton, so **no `enable_shared_from_this`** and
`invokeOnGuiThread` targets `qApp`, unlike the per-tab
`FolderNavigationController`/`ThumbnailController` (phase 9). Everything is keyed by handle, not row
index (`FileListModel`'s phase 13a convention), because the validation sweep drops rows.

`reload()` (login) shows the stored names immediately, then starts the sweep, so there's no blank
panel while it runs. The sweep fires one `resolveFolder` per pin and reconciles in a shared `Sweep`
struct (a snapshot vector plus a `remaining` counter); the last callback to arrive commits once via
`replaceAll`, refreshing surviving pins' names (rename following) and dropping the rest. Two details
worth keeping: a dropped pin is marked by zeroing its handle in the snapshot rather than erasing mid
sweep (0 is never a real handle, so it doubles as the marker without disturbing the other callbacks'
indices), and the commit is **skipped entirely when the reconciled list equals the current one** —
the common case — so an unchanged sweep causes neither a store write nor a model reset, same
"don't emit an identical value" guard as `FolderNavigationController::refreshBreadcrumb`. Auto-removal
is silent (a `lcQuickAccess` log line only, no `NotificationController` toast), per the user's
decision and matching phase 8/10's "degrade quietly" precedent. The **generation guard** is copied
verbatim from `FolderTreeModel`: `reload()`/`reset()` bump `mGeneration`, every callback captures it
and bails on mismatch — without it a sign-out part-way through a sweep would let the stale result
reconcile against the now-empty list and write it back.

`activate(handle, inNewTab)` covers the case the login sweep can't: a folder deleted *during* the
session, on another device. It re-checks via `resolveFolder` and answers with either
`activated(handle, inNewTab)` or `missing(handle, name)` — navigation and dialogs stay QML's job,
this model knows nothing about tabs. `Main.qml`'s new `Connections` on `quickAccessModel` turns
`activated` into `navigateTo`/`addTabAt` and `missing` into a confirmation `Dialog`
(`missingPinDialog`, alongside the existing `signOutConfirmDialog`): Yes unpins, Cancel leaves the
pin alone so clicking it again asks again, exactly as scoped.

**`FileAction::TogglePin`** — one enum value plus one `defaultFileActions()` row
(`{TogglePin, FoldersOnly, SingleOnly}`), no `FileActionResolver` change, as phase 13b intended.
Deliberately *one* action rather than a `Pin`/`Unpin` pair: the resolver only sees a
`SelectionSummary` (counts by kind) and so cannot know whether the selected folder is already pinned.
`FileContextMenu.qml` resolves that instead — a `selectionPinned` bool sampled in `onAboutToShow`
(not bound: `quickAccessModel` has no per-handle change signal to drive a binding, and the answer
can't change while the menu is open) feeding a new `labelFor()` that the delegate's `label` now calls
instead of indexing `actionLabels` directly. Note this made
`FileActionResolverTest.DefaultTableOffersOpenInNewTabForSingleFolder`'s exact-size assertion stale
(a single-folder selection now yields two actions); updated in place.

**QML**: `qml/components/SidePanel.qml` (new) is the whole left panel — `QuickAccessSection` + a 1px
divider + the pre-existing `FolderTreePanel` in a `ColumnLayout`. It exists purely so
`FolderTreePanel.qml` could stay a bare `TreeView` with its phase 10 design notes (including the
hard-won `pointerNavigationEnabled` warning) untouched; `Main.qml`'s `SplitView` child changed from
`FolderTreePanel` to `SidePanel`, which moved `SplitView.minimumWidth`/`maximumWidth` and the
persisted-width one-shot `Component.onCompleted` onto the new wrapper (`window.treePanelWidth` and
its `Settings` alias are otherwise unchanged). `SidePanel.qml` is the one file in `qml/components/`
with **no** `QtQuick.Controls.FluentWinUI3` import — it instantiates no Controls type, and qmllint
flags the import as unused.

`qml/components/QuickAccessSection.qml` (new) is a header `Label` plus a `ListView` of `ItemDelegate`
rows. `visible: quickAccessModel.count > 0` hides the header too when nothing is pinned, rather than
leaving an empty labeled box above the tree. The list is sized to `contentHeight` but capped at 40%
of the panel so a long pin list can't push the tree out; the cap reads a `required property real
availableHeight` passed down from `SidePanel` rather than `root.height` — this `ColumnLayout`'s own
height derives from its children, so capping a child against it would be a binding loop. The
highlight is deliberately the *same* expression as `FolderTreePanel.qml`'s `isCurrent`
(`!navController.atRoot && handle === navController.currentHandle`, `Qt.rgba(highlight…, 0.35)`), so
both halves of the panel indicate the current folder identically. `focusPolicy: Qt.NoFocus` on the
delegate, same reason as `TabStrip.qml`/`FolderTreePanel.qml` (otherwise clicking a row strands
keyboard focus and deadens the file view's arrow keys).

`qml/components/FolderPinMenu.qml` (new) is the right-click menu shared by tree rows and pin rows —
both address a folder by `(handle, isRoot)`, unlike `FileContextMenu.qml`, which is selection-driven
and knows nothing about which row was clicked. `popupFor(handle, isRoot, name)` fills the target
properties then pops up. One instance per view (`FolderTreePanel` and `QuickAccessSection` each hold
one), never per delegate — phase 13b's lesson. The pin item is disabled for the Cloud Drive root,
which is permanently the tree's own top row. The tree's new right-click `TapHandler` needs no
select-then-popup step (unlike the file views): the tree has no `selectionModel` at all, so the
clicked row is handed straight to the menu. Its delegate gained `required property string name`
purely to label a new pin — the row's own text still comes from `TreeViewDelegate`'s stock
`contentItem` reading `Qt::DisplayRole`.

**Tests**: `tests/QuickAccessServiceTest.cpp` (14 cases, Qt-free) covers load/ordering, load-failure
degradation, append-at-end, duplicate rejection without a store write, unpin, `isPinned`,
`replaceAll` as a single write, `clear` *not* touching the store, `resolveFolder` delegation, and
each way `isUsable` can say no (unresolvable / outside the Cloud Drive / a file).
`tests/QuickAccessModelTest.cpp` (10 cases) is the same narrow exception to "`src/qml` is GUI glue,
untested by convention" as `FileListModelTest`/`TabsControllerTest`/`FolderTreeModelTest`, for the
same reason — the sweep's reconciliation and generation guard are pure, bug-prone bookkeeping. It
pins the stored-names-shown-before-validation behavior, rename following, both dangling cases,
the no-rewrite-when-unchanged guard, `activated`/`missing` (including `missing` carrying the clicked
label and *not* removing the pin), duplicate-pin rejection, and the generation guard via `SaveArg` →
`reset()` → fire.

**`tests/TestApp.h` (new)**: `FolderTreeModelTest.cpp`'s function-local static `QCoreApplication` had
to be extracted here, because Qt permits exactly one per process and a second copy in
`QuickAccessModelTest.cpp` would abort with "There should be only one application object". It's a
static inside an `inline` function, so every translation unit including the header shares the one
instance; `FolderTreeModelTest.cpp` now includes it instead of declaring its own.

`QSettingsPinnedFolderStore` gets **no** adapter-level test, unlike `WindowsSessionStore`: `QSettings`
writes to the real per-user registry, so a round-trip test would either pollute the developer's
actual `HKCU\Software\MegaExplorer` key or need an org/app-name seam added purely for testing. The
interesting logic all lives in the service and model, which are covered.

**Known limitations**: ~~pins are stored per machine, not per account~~ — fixed in Phase 11a below.
Because "in the Rubbish bin" counts as deleted, restoring a folder from the Rubbish bin does not
bring its pin back. Pin order is insertion order with no reordering UI (drag-to-reorder was
explicitly scoped out).

## Phase 11a — per-account quick-access scoping (done)

Phase 11's own "Known limitations" note above called the machine-wide pin key "accepted rather than
designed around, given this is a single-account app in practice" — revisited because the actual
behavior isn't "shows the previous account's pins, then self-cleans": logging out only clears
`QuickAccessService`'s in-memory list, so the on-disk JSON survives untouched; logging into a
*different* account then runs the login-time validation sweep against handles that belong to the
old account, none of which resolve, and the sweep commits the (now empty) survivor list back over
the same shared key — silently **destroying** the first account's pins, not merely hiding them.

No per-account identifier existed anywhere in the app before this (checked `IMegaClient`,
`AuthService`, `AuthController`, `ISessionStore`/`WindowsSessionStore` — all handle a single opaque
session token and let no email/user-handle survive past login). Added
`IMegaClient::currentUserHandle() -> Result<std::uint64_t>`, mirroring `currentSessionToken()`'s
shape exactly (synchronous, fails if not logged in); `MegaSdkClient` implements it over
`MegaApi::getMyUserHandleBinary()`. `IPinnedFolderStore::load`/`save` both gained an `accountKey`
parameter — the account's user handle rendered as a decimal string, opaque and needing no escaping
for a QSettings key path — and `QSettingsPinnedFolderStore` now nests the JSON blob at
`quickAccess/accounts/<accountKey>/pinnedFolders` instead of the old flat
`quickAccess/pinnedFolders`.

Account-identity resolution lives entirely inside `QuickAccessService`, which already holds the
`IMegaClient` it needs: `load()` resolves `currentUserHandle()` first and caches the result in a new
`mAccountKey` member (cleared, alongside `mPins`, whenever resolution fails or on `clear()`), and
every mutator (`pin`/`unpin`/`replaceAll`) passes it to `mStore->save()`, skipping the write entirely
if no account is currently resolved. This keeps `QuickAccessModel`/`Main.qml` completely unchanged —
`reload()`/`reset()` still take no arguments — matching the existing services-pass-through/
QML-marshals split.

**Deliberately no migration**: pre-existing flat-key pin data is simply abandoned/orphaned in the
registry once this ships, rather than copied into whichever account happens to log in first. Decided
during planning as the simpler option, given the flat key only ever reliably held one real-world
account's pins anyway.

Tests: `MockMegaClient`/`MockPinnedFolderStore` gained the new methods/parameter;
`QuickAccessServiceTest`'s fixture stubs a fixed account handle by default and adds
`PinsAreIsolatedPerAccount` (two accounts' pins don't cross-contaminate, verified via distinct
`accountKey` arguments reaching the mock store) and
`LoadWithNoLoggedInAccountDegradesToEmptyAndSkipsTheStore`. `QuickAccessModelTest` needed only the
same fixture stub — it has no account awareness of its own, so its existing coverage was otherwise
unaffected.

## Phase 12 — rename / delete (move to rubbish) (done)

The codebase's **first mutating SDK calls**: everything `IMegaClient` exposed before this
(`login`/`fetchNodes`/`getChildren`/`search`/`getPath`/`getNodeInfo`/`download`/`getThumbnail`) only
read. Two methods were added, both `Result<void>` on a `MegaRequestListener`, so they reuse the
existing self-deleting `SimpleResultListener` and the private `resolveNode(handle, false)` helper
verbatim — no new listener class was needed:

- `renameNode(handle, newName, onDone)` → `MegaApi::renameNode`.
- `moveToRubbish(handle, onDone)` → `MegaApi::moveNode(node, getRubbishNode())`. **Not
  `MegaApi::remove()`**, which destroys the node outright; a MEGA "delete" is a move into the Rubbish
  bin (the same fact phase 11 had to work around from the other direction, where a deleted node's
  handle keeps resolving). `getRubbishNode()` returning null (nodes not fetched) is treated as its
  own failure rather than assumed away.

**Move (`moveNode` to an arbitrary parent) was cut from the phase**, per the user's decision: with no
destination picker anywhere in the UI, the only sensible trigger is drag & drop, which belongs with
phase 14. That decision propagates into the UI — the Rubbish-bin snackbar has **no Undo button**,
because undoing would need exactly the general move that doesn't exist yet.

`src/core/FileOperationService.{h,cpp}` (`MegaExplorerCore`) is the same thin
"validate + pass through, no state" shape as `FolderTreeService`, with the async fan-out left to
`src/qml` per `QuickAccessService.h`'s written-down split. Its only rule is a static
`isValidName(name)`: reject empty/whitespace-only, and reject `/` or `\` (those would break
`DownloadService`'s local-path composition downstream). Deliberately **no duplicate-name check** —
MEGA permits two same-named children in one folder — and no trimming, so what the user typed reaches
the SDK verbatim. An invalid name fails the callback immediately without touching the SDK.

The context menu needed **no resolver changes at all**, which is the phase 13b design paying off:
two rows in `defaultFileActions()` (`Rename` → `ActionTarget::Any`/`ActionArity::SingleOnly`,
`MoveToRubbish` → `Any`/`Any`) plus two `fileActionId()` cases. `ActionArity::SingleOnly` *is* the
entire implementation of the requirement "don't offer rename while several items are selected".
`FileContextMenu.qml` gained two labels and two new signals (`renameRequested`,
`moveToRubbishRequested`) that it delegates to the owning view — it stays ignorant of inline editing
and dialogs, which are per-view concerns.

`NotificationController` gained a second channel, `notifyOperation(context, succeeded, failed)` →
`operationFinished`, following the existing "C++ passes structured fields, QML composes the
sentence" convention. `qml/components/OperationSnackbar.qml` is `DownloadSnackbar.qml` minus its
"Open" button.

`FolderNavigationController` is where both operations land (it already owns the tab's
`FileListModel`, the `NotificationController*`, the refetch path and `enable_shared_from_this`), with
one added constructor argument. `moveSelectionToRubbish()` walks `selectedEntries()` in row order,
fires one `moveToRubbish` per entry, and tallies into a shared
`RubbishBatch{remaining, succeeded, failed}` — **only the last callback to land** refreshes the
listing and reports the tally, so N deletions produce one refetch and one snackbar (the same shape as
`QuickAccessModel::validateAll`'s `Sweep`). Every async lambda captures `shared_from_this()` and
marshals through `invokeOnGuiThread(this, ...)`, the two-stage phase 9 rule that makes closing a tab
mid-operation safe.

One thing the plan didn't foresee: refreshing with a bare `refreshCurrentFolder()` would silently
throw the user out of an active search, since it overwrites the visible model with the folder
listing. `setSortOrder` already had the correct two-branch logic for this, so that body was extracted
into a private `refreshVisibleListing()` (re-run the search *and* refresh the cached folder listing
behind it, or plain refetch when not searching) and both mutations go through it. Net effect is less
duplication than before the phase.

### Inline rename UI: why not `TableView.editDelegate`

Qt 6.5+ does ship a cell-editing mechanism (`TableView.editDelegate` / `edit(index)` /
`editTriggers`), and it was investigated and rejected:

1. `edit()`'s documented behavior includes *"The current index in the selection model will also
   change to modelIndex"* — but this project's `TableView` deliberately has **no `selectionModel`**
   (phase 13a put handle-keyed selection in `FileListModel` instead) and sets
   `keyNavigationEnabled: false`.
2. The default `editTriggers` are `DoubleTapped | EditKeyPressed`, and double-click is already
   "open folder / download file", so it would have had to become `NoEditTriggers` plus a manual
   `edit()` call regardless.
3. `GridView` has no equivalent at all, so one hand-rolled editor was needed either way.

So `qml/components/InlineRenameField.qml` is a plain `TextField` used by both views: it selects the
basename (extension excluded, `lastIndexOf(".") > 0` so a dotfile isn't mangled; folders select
everything), commits on Enter via `TextInput`'s own `accepted`, cancels on Escape, and commits on
focus loss. It carries a `settled` flag because confirming hands focus back to the view, which would
otherwise re-fire the focus-loss commit for the same edit. Each view keeps a single `renamingHandle`
(0 = not renaming, the usual meaningless-sentinel convention) and a `Loader` in the name cell/tile
that goes active only for that row, so a large listing pays nothing. The `Component` inside each
`Loader` is declared **inline** rather than shared at root level: `InlineRenameField`'s properties are
`required`, and a `Loader` has no way to supply those to a component it didn't declare, whereas
inline they simply bind to the delegate's scope.

**Gotcha — the background `TapHandler` steals the first click into the field.** Both views handle
selection with one view-level `TapHandler` (`parent: tableView` / `parent: root`, phase 13a) whose
default `DragThreshold` gesture policy takes only a *passive* grab. A passive grab does not consume
the event, so tapping inside the rename `TextField` still fires that handler, and its
`root.forceActiveFocus()` would commit the edit the instant the user clicked into their own text.
Fixed with a guard at the top of `onTapped`: if a rename is active and the hit row is the cursor row
(which is the renamed row by construction — `beginRename` calls `selectRow(row, Qt.NoModifier)`,
already "deselect everything else and select just this one", so `FileListModel` needed nothing new),
return without touching focus. A tap on any *other* row deliberately falls through and commits via
focus loss.

Two smaller focus/key details: teardown of the field goes through `Qt.callLater(root.endRename)`
rather than running inline, so the `Loader` doesn't destroy the object whose signal handler is still
on the stack; and each view's `Keys.onPressed` returns early while `renamingHandle !== 0`, because
the field is a focus-chain descendant and — unlike arrows and Ctrl+A, which `TextInput` consumes —
F2 and Delete would otherwise propagate up to the view. `endRename` re-focuses the view, the same
reason `focusPolicy: Qt.NoFocus` appears throughout this UI: without it the arrow keys go dead.

F2 and Delete were both wired (Delete opens the same confirmation dialog as the menu action) since
this is an Explorer-alike; the user confirmed Delete was wanted.
`qml/components/ConfirmRubbishDialog.qml` is one instance per view, alongside that view's
`FileContextMenu`, with `parent: Overlay.overlay` so it still centers on the window despite being
declared from inside a view; it samples the selection count and first name at open time rather than
binding, so the wording can't shift under an already-open dialog.

**Tests.** `tests/FileOperationServiceTest.cpp` (9 cases) pins the validation rule from both sides,
including `EXPECT_CALL(...).Times(0)` proof that a rejected name never reaches the SDK, and that
Windows-illegal-but-MEGA-legal characters (`:?*`) are *not* rejected.
`tests/FolderNavigationControllerTest.cpp` (6 cases) is another narrow, deliberate exception to
"`src/qml` is GUI glue, untested by convention" — same justification as
`FileListModelTest`/`TabsControllerTest`/`QuickAccessModelTest` — covering only the fan-out
bookkeeping: all-succeed, partial failure, single item, empty selection, plus rename's invalid-name
and success paths, each asserting the refetch happened **exactly once** (counted through
`getRootChildren` calls). As phase 11 predicted for itself, the exact-action-list assertions went
stale again: `FileActionResolverTest`'s default-table cases and two `FileListModelTest`
`availableActions` cases had to be updated, and the two that asserted "no actions at all" for
folder/mixed selections are now "only `moveToRubbish`" — worth expecting on every future action added
to the table.

**Known limitations (accepted).** The side panel (folder tree and quick access) does not reflect a
rename or deletion until the next login; a deleted pin is still caught at click time by phase 11's
`activate()` re-check, so the damage is limited. The refetch goes through `beginResetModel()`, so
scroll position jumps to the top after an operation (selection and cursor survive, being
handle-keyed). Other tabs showing the same folder are not invalidated — this app has never had any
cross-tab invalidation mechanism. And move is not implemented; see the roadmap note.

## Phase 14a — move via drag & drop (done)

Collects phase 12's deferred `moveNode` (see its log entry and the roadmap note). **Upload was
explicitly left out** and stays as phase 14b: this phase is only about moving nodes that already
exist in the account. Scope: drag starts in the grid/list views only; drops land on a folder row, a
view's empty space (= the folder being shown), a folder-tree row (including the root), a
quick-access pin, or a breadcrumb segment (added as a follow-up, see the last section below).

### SDK layer: `checkMove` is the interesting half

`moveNode(handle, newParentHandle, newParentIsRoot, onDone)` is the trivial part — `moveToRubbish`
already called `MegaApi::moveNode`, just with `getRubbishNode()` hardcoded as the destination, so it
reuses `SimpleResultListener` and `resolveNode` unchanged.

`checkMove(handle, newParentHandle, newParentIsRoot)` is the **third synchronous method** in
`IMegaClient`, after `currentSessionToken`/`currentUserHandle`. It wraps
`MegaApi::checkMoveErrorExtended` (in-memory ancestor walk, no API round-trip) and has to be
synchronous because a hovering drag queries it to decide whether to paint an accept highlight.
Its result's `errorCode` is the payload, not its message: `kENoEnt` (either end gone),
`kECircular` (folder into its own descendant), `kEAccess`. `kECircular = -10` was added to
`MegaErrorCodes.h` with its matching `static_assert`.

It is deliberately **stricter than the SDK**: a move to the node's current parent is reported as
`kEArgs` rather than accepted as a pointless no-op. That check lives here, not in a caller, because
this is the only layer holding real `MegaNode`s — a caller pointing at the root has only the
`isRoot` sentinel and never the root's actual handle, so it *cannot* make the comparison. An earlier
draft instead added `FileEntry::parentHandle` and compared in the controller; that was dropped once
the root case showed it couldn't work uniformly.

`resolveNode` became `const` to let `checkMove` be `const`. Nothing else had to change:
`unique_ptr::operator->()` is const-qualified but hands back a non-const `MegaApi*`, the same trick
`currentSessionToken() const` already relied on.

`FileOperationService` gained the matching `move`/`canMove` pair, keeping the phase 12
"async execute + sync pre-check" split that `rename`/`isValidName` established. `move` re-runs
`canMove` itself, so skipping the pre-check can't smuggle through a move the SDK would refuse.

### Controller: one batch helper, two callers

`moveSelectionToRubbish`'s `RubbishBatch` was renamed `BulkOperationBatch` and its tail extracted
into `accountForBulkOutcome(batch, result, context)` — N results collapse into one
`refreshVisibleListing()` and one `notifyOperation()`, which is now shared verbatim by both
fan-outs.

`moveHandlesTo(handles, target, targetIsRoot)` takes the handles **explicitly** rather than reading
`mFileListModel`'s selection the way `moveSelectionToRubbish` does. A drop can land on the folder
tree or a quick-access pin, both of which are shared across every tab, so "the selection" has no
single meaning there; the drag snapshots its payload at gesture start and carries it.
`canDropHandlesOn` is all-or-nothing — one un-movable item greys out the whole drop rather than
silently moving the rest.

`FileListModel::entryAt(row)` was added so a view-level drop handler can ask what sits under the
cursor after resolving a position to a row. QML can't call `data()` usefully: the `Role` enum isn't
exposed there.

### QML: why a proxy item, and why view-level drop areas

`qml/components/DragProxy.qml` is a single window-wide item parented to `Overlay.overlay`, and it is
the thing that actually carries `Drag.active`. Putting `Drag.active` on a delegate does not work
here: a delegate lives inside GridView/TableView's Flickable viewport, which **clips** it, so the
drag visual would vanish exactly when dragged towards the side panel — which is where half the drop
targets are. The proxy also has to *move*, since an internal drag emits its events from the attached
item's position changes; `moveTo()` is simultaneously "update the ghost" and "tell the DropAreas
where the cursor is".

It reaches the views as a required property drilled through `TabContentPane`/`SidePanel`, the same
route `navController` takes — a separately-loaded `.qml` file can't reach `Main.qml` by id.

Drop handling is split by what the target *is*:

- **File views: one view-level `DropArea`**, resolving the row by position (`GridView.indexAt` /
  `TableView.cellAtPosition`), mirroring the view-level `TapHandler` those files already use. This
  also sidesteps `FileTableView`'s cell-granular delegate, which can't cover a whole row, and gives
  the "dropped on empty space" case somewhere to live — it has no delegate at all.
- **Tree and quick access: a `DropArea` per delegate**, because a row there *is* the target and the
  existing handlers in those files are already per delegate.

Both read the payload off `root.dragProxy` rather than the event's `drag.source`. They're the same
object — `keys: ["application/x-megaexplorer-nodes"]` lets nothing else in — but `drag.source` is
typed `QObject`, so every field access through it is an unchecked dynamic lookup that `qmllint`
flags.

`GridView` gained `acceptedButtons: Qt.NoButton`, matching what `FileTableView` already did:
Flickable pans on left-drag by default, which is now how a move drag starts instead. Wheel
scrolling is unaffected.

`qml/components/DragAutoScroller.qml` restores edge scrolling, which Qt provides nothing for and
which disabling the pan took away. One subtlety worth knowing: with a stationary cursor at the
edge, content scrolls but drag events are *not* re-delivered, so the last `track()` call stands and
scrolling continues — but the highlighted row doesn't follow until the pointer moves again.

### Deliberately not done

- **Other tabs are not refreshed.** Only the tab the drag started from refetches. There has never
  been a cross-tab invalidation mechanism (phase 12's log says the same); phase 16's remote-change
  reflection is expected to subsume it.
- **The folder tree is not refreshed**, so a moved folder shows in both its old and new position
  until the next login. `FolderTreeModel` never removes loaded nodes by design and has no partial
  reload API. Same phase 16 rationale.
- **Spring-loaded (hover-to-expand) tree rows**, and auto-scrolling of the quick-access list.
- **Dragging out of the tree or quick access**, and a "Move to..." context-menu action. Drag & drop
  is the only trigger.
- **Undo.** A general move now exists, so a Rubbish-bin undo is finally *expressible* — but it would
  need each item's pre-move parent, which nothing records.

D&D itself has no automated coverage: `tests/` is entirely C++ gtest with no QML test harness. The
C++ half is covered (`FileOperationServiceTest`, `FolderNavigationControllerTest`); the gesture is
manual-test-only.

### Follow-up: the breadcrumb as a fifth drop target

Dragging onto an ancestor to move something "back up" is natural in Explorer, and the breadcrumb was
the one place showing ancestors that couldn't take a drop. No C++ was needed: `Breadcrumb.qml`'s
delegates already carry the `{handle, isRoot}` pair that `canDropHandlesOn`/`moveHandlesTo` take, so
this is `QuickAccessSection.qml`'s per-delegate `DropArea` copied verbatim with `modelData.isRoot`
in place of the hardcoded `false`.

Only the segment `Label` is the target — not the `>` separator, which is still reserved for a future
subfolder dropdown. Feedback is the same highlight outline, drawn by a `z: -1` `Rectangle` because
`Label` has no background and a plain child would paint over the text; it's anchored rather than
laid out so `relayout()`'s `implicitWidth` sum (the overflow cutoff) is unaffected.

Two cases need no code. The last segment is the current folder, which `checkMove` already rejects as
"already in that folder" — the same rule that makes the empty-space drop reject in place. Segments
folded behind the `«` overflow indicator are `visible: false` and therefore receive no drag events,
so they're unreachable as targets; `«` itself deliberately gets no `DropArea` (it stands for several
folders at once, and the tree panel reaches those anyway).

Also deliberately not done: hover-to-navigate (spring loading) on a segment, matching the other four
targets, which likewise don't navigate.

## Phase 14b — upload via drag & drop (done)

Files dragged in from Explorer land on the *same five* drop targets 14a built (folder row, a view's
empty space, folder-tree row including the root, quick-access pin, breadcrumb segment). No new drop
target, no new `.qml` file. What was actually new: telling an external file drop apart from 14a's
internal node drag, and the `MegaApi::startUpload` transfer plumbing behind it.

Files only. A drop containing folders raises a "skip them and upload the rest?" dialog; a drop of
*nothing but* folders is reported as "nothing to upload" instead — asking "upload the remaining 0
file(s)?" would be a terrible question, so the empty check runs first.

### SDK layer: three additions, two of them synchronous

`upload(localPath, parentHandle, parentIsRoot, onProgress, onDone)` mirrors `download`'s
two-callback shape for the same reason (`MegaTransferListener`, not `MegaRequestListener`), with
`UploadListener` a near-copy of `DownloadListener`.

**`MegaUploadOptions` turned out to be unnecessary.** The non-deprecated overload
(`megaapi.h:17506`) takes `const MegaUploadOptions*`, and `megaapi.cpp:3801-3805` only copies the
struct when it is non-null — so passing `nullptr` yields every default (name from the local path,
local mtime preserved, `isSourceTemporary=false`), which is exactly what this app wants. No options
object is constructed anywhere.

`UploadOutcome` carries only the created node's handle. It deliberately has **no counterpart to
`DownloadOutcome::alreadyPresent`**: the SDK's upload-side shortcut (an existing node with a
matching fingerprint, whose mtime is simply updated) is indistinguishable from a real upload both to
the user and to us — there is no upload equivalent of the `transferredBytes == 0 && totalBytes > 0`
tell that the download path relies on.

`checkUpload(parentHandle, parentIsRoot)` is the interface's **fourth synchronous method**, and
exists for the same reason `checkMove` does — a hovering drag queries it continuously. Unlike
`checkMove` there is no `checkUploadErrorExtended` to wrap, so the conditions are spelled out:
`kENoEnt` when the handle doesn't resolve *or* resolves outside the Cloud Drive (`isInCloud`, the
same "still usable?" test `NodeInfo::inCloud` uses, since a deleted folder keeps resolving from the
Rubbish bin), `kEArgs` when it's a file, `kEAccess` when `getAccess` is below `ACCESS_READWRITE`.
That last one is purely defensive — this app only ever shows the user's own drive — and was the
phase's second-largest risk, since `ACCESS_UNKNOWN` would have made every target silently reject;
verified on a real account before the remaining drop targets were wired up.

`findChildFiles(parentHandle, parentIsRoot, names)` is the **fifth**, backing the same-name dialog.
It uses `getChildNodeOfType(parent, name, TYPE_FILE)`, **not** `getChildNode`, which prefers a
same-named *folder*. MEGA lets a file and a folder share a name, and uploads are files-only, so
"replacing" a folder with a file would be both destructive and unasked-for; a destination holding
only a same-named folder is simply not a conflict. `nodeListToEntries` was split so its per-node
half (`nodeToEntry`) could be reused here.

`MegaErrorCodes.h` was **not** touched — `kENoEnt`/`kEArgs`/`kEAccess` already cover every branch,
and that header's rule is "only the values something actually branches on".

### `UploadService`: `DownloadService` with two deliberate divergences

Same serial queue, same mutex discipline copied verbatim (lock only around `mQueue` mutation and
observer copy-out; observers invoked unlocked; `mClient->` called with no lock held, or the first
`MockMegaClient` test self-deadlocks; `mQueue.front()` is always the active job; finished jobs
erased *before* their notification).

**`startNextIfIdle` is a `for(;;)` loop, not tail recursion.** Each job re-validates its destination
via `checkUpload` before starting — the hover-time check only covered the moment of the drop, and
the folder can be deleted from another device while the queue drains. That failure path is
*synchronous* and *reachable in production*: dropping 200 files onto a folder that has just
disappeared would recurse 200 frames deep. `DownloadService`'s recursion is only ever exercised
through a mock, so it was left alone.

**No duplicate suppression, no local-file existence check.** `DownloadController::hasJobForHandle`
exists because a double-click or menu item can be fired repeatedly; a drop is one explicit gesture.
There is also nothing to key on — MEGA allows same-named siblings, and an uploaded node has no
handle until its transfer completes. Existence is left unchecked because `src/core` has no
filesystem access by design; a file gone by its turn just fails through the ordinary path.

`UploadJob::replaceHandle` rides along completely uninterpreted by this service and comes back
untouched in the finished-job notification — see the next section for why the actual deletion lives
one layer up.

### "Replace" is not native to MEGA

MEGA has no overwrite. "Replace" is therefore two steps: **upload first, then move the old node to
the Rubbish bin.** Never the reverse — deleting first would lose data if the transfer then failed.
The cost is that it isn't atomic: a crash in between leaves both files, which is strictly the safer
failure.

The second step lives in `UploadController` via `FileOperationService::moveToRubbish`, not in
`UploadService`, so the queue stays the single source of *order* without owning a two-phase
transaction. `Batch::pendingReplaces` then **gates the batch flush**: emitting `destinationChanged`
before the bin moves land would have the refetched listing show both the old and the new file side
by side.

When two dropped files share a leaf name (`C:\a\x.txt` and `C:\b\x.txt`) there is still only one
node to replace, so `enqueueAll` gives the handle to the **first** of them; binning it twice would
just fail the second time with `kENoEnt`. The rest upload normally and end up as same-named
siblings, which MEGA considers perfectly legal.

### `UploadController`: app-global, and the two-dialog chain

App-global like `DownloadController` (stack local in `main.cpp`, no `shared_from_this`), because
three of the five drop targets — tree, pins, breadcrumb — are chrome shared by every tab and have no
owning tab at all. That also settles where `canUploadTo` belongs: here, not on the per-tab
`FolderNavigationController`.

`dropUrls` is the single entry point for all five `DropArea`s. Classification has to be C++: QML
can't tell a directory from a file. It drops non-local URLs (a browser drag), counts directories,
and `QDir::toNativeSeparators`es the rest — the path crosses into the SDK's own `LocalPath`, which
splits on `\` on Windows (phase 4's gotcha).

Dialog order is **folder check → same-name check**, and the chain needs no explicit state machine:
answering the folder dialog calls `uploadFiles()`, which runs the collision check itself and raises
the second dialog only if it finds something. Both dialogs **carry the destination** rather than
having C++ remember it, so it lives exactly as long as the question does (`missingPinDialog`'s
shape). Both answers **re-derive the collision set** instead of round-tripping handles through QML —
more robust, and it picks up any change made to the destination while the dialog was open. It's an
in-memory lookup, so re-deriving is nearly free.

The three-way answer (Replace / Skip / Cancel) can't be expressed with `standardButtons`, so
`nameConflictDialog` supplies its own `DialogButtonBox` footer with two `ActionRole` buttons and one
`RejectRole`, calling `uploadController` directly rather than through `onAccepted`.

Batch accounting collapses a whole drop into **one** snackbar (a second drop arriving mid-flight
joins the same batch on purpose — one snackbar beats two competing ones). Individual failures are
`qCWarning(lcUpload)` only, per `DownloadController`'s no-double-reporting rule.

One correction to the plan: the flush condition was going to be "no active job and no pending
replaces", read off the service's queue. That is **wrong**, and a test caught it — the finished
notifications arrive through a queued invoke, so when the mock completes transfers synchronously the
queue is already empty by the time the *first* notification is handled, and the batch flushed once
per job instead of once per batch. `Batch::pendingJobs` is now counted by the controller itself.

### Refresh fan-out, and how it squares with 14a

`refreshVisibleListing()` is private and *per tab*, while `UploadController` is app-global. The
bridge is a guarded public entry point, `FolderNavigationController::refreshIfShowing(handle,
isRoot)`, which each tab wires to `uploadController.destinationChanged` from `TabContentPane.qml` —
every tab decides for itself. It calls `refreshVisibleListing`, not `refreshCurrentFolder`, so a tab
that happens to be searching isn't dropped out of its search.

Rejected alternatives: carrying a `sourceNav` the way `DragProxy` does (the tree/pins/breadcrumb all
bind `navController` to the *current* tab, which usually isn't showing the destination — and a
needless `refreshVisibleListing` on a searching tab costs a whole extra search round-trip); matching
the current tab in QML (leaks the `atRoot`-vs-`currentHandle==0` sentinel rule and the
`mHasLoadedOnce` guard into QML, and misses a background tab showing the destination); exposing
`refreshVisibleListing` raw.

This **fans out wider than 14a**, which refreshes only the drag's source tab. That's not an
inconsistency: an upload has no "source tab" to prefer, so every tab showing the destination is the
minimal correct answer. And 14a's other deferral doesn't apply either — the folder tree shows *only
folders* while 14b uploads *only files*, so an upload cannot change the tree's contents. Nothing is
being postponed to phase 16 here.

### QML: four points repeated across all five drop areas

1. **`keys` gains `"text/uri-list"`.** An internal Qt drag is matched against `Drag.keys`, but a
   drop coming in from Explorer is matched against its `QMimeData` format strings — without this,
   external drops are silently ignored.
2. **The hover handlers branch on `dragProxy.active`, not `drag.hasUrls`.** All five previously
   dereferenced `dragProxy.sourceNav` unconditionally, which would `TypeError` on an external drop.
   `hasUrls` is a claim about the *event*; `active` is a claim about the very object the internal
   branch then dereferences. (Correct for the three chrome drop areas, but it broke the two file
   views — see the follow-up at the end of this phase.)
3. **`onDropped` branches on `drop.hasUrls` instead** — a deliberate departure from the plan.
   `DragProxy.finish()` calls `Drag.drop()` to deliver that very event, and `Drag.active` is cleared
   as a side effect of the same call, so its value inside the handler depends on Qt's internal
   ordering. The event's own payload doesn't. (Qt sources aren't installed locally to settle the
   ordering question, and relying on it would risk a 14a move regression for no benefit.)
4. **`drag.accepted` / `drop.accept(Qt.CopyAction)` are set on the external branch only.** Touching
   `drag.accepted` on the internal branch would break the move path, which relies on implicit
   acceptance via key matching.

The existing `accepting`/`dropRow`/`dropOnCurrentFolder` state and the highlight `Rectangle`s are
payload-agnostic and were reused as-is; only the question being asked branches. `DragAutoScroller`
is unchanged, and its `track()` call sits *outside* the branch — edge scrolling should work for
either kind of drag.

Two intentional behavior differences from the move path, both correct:

- **The last breadcrumb segment now highlights.** It's the current folder, which `checkMove` rejects
  as "already in that folder" but which is a perfectly good upload destination.
- **The file views' viewport frame stays lit for most of an external drag.** There is no "already
  lives there" case for a file coming from outside, so `dropOnCurrentFolder` is almost always true.
  That is Explorer's behavior.

### Testing

`tests/UploadServiceTest.cpp` (12) plus `tests/UploadControllerTest.cpp` (12).
**`UploadController` is in the test target**, unlike `DownloadController` — the latter is excluded
because `QDesktopServices` pulls in QtGui, while this one touches only QtCore
(`QUrl`/`QFileInfo`/`QDir`/`QStringList`/`QMetaObject`), which the target already links. The "src/qml
is untested by convention" rule bends exactly as far as it already does for
`FolderNavigationController`/`TabsController`/`QuickAccessModel`: `dropUrls`' classification and the
collision bookkeeping are real logic, not rendering. Signals are observed with plain
`QObject::connect` lambdas rather than `QSignalSpy`, which would add a `Qt6::Test` dependency.

**gmock trap worth remembering**: `Result<void>::success` defaults to `false`, so the *default*
action for an unstubbed `checkUpload()` is a **failure**, which makes `startNextIfIdle` fail every
job before it ever calls `upload()`. It surfaces as "every test fails", not as a compile error. Both
fixtures therefore open with a blanket `EXPECT_CALL(...).Times(AnyNumber()).WillRepeatedly(Return(
Result<void>::ok()))`; tests wanting the opposite declare their own, later, `EXPECT_CALL`, since
gmock matches expectations newest-first.

The gesture itself remains manual-test-only — `tests/` is C++ gtest with no QML harness, same as
14a.

### Deliberately not done

- **Folder upload.** `startUpload` accepts a directory and would recurse, but a folder in a drop is
  skipped by explicit user confirmation instead.
- **Cancel.** `MegaCancelToken` is created and kept alive (the SDK requires it until
  `onTransferFinish`), but nothing cancels — same as `DownloadService`. Sign-out warns about
  in-flight transfers rather than cancelling them.
- **Parallel transfers.** One at a time, matching download.
- **Dragging files *out* of the app** to Explorer.
- **Per-file progress UI.** The footer shows the active file plus an "n remaining" count.

### Follow-up: point 2 above silently killed 14a's in-view drops

Found 2026-08-03, while chasing "drops onto the tree/pins/breadcrumb work, drops inside the grid or
the list do nothing". Not a design-pass regression despite the timing — `FileGridView.qml`'s drag &
drop code hadn't changed since 14b at all.

`DragProxy.begin()`'s `Drag.active = true` delivers the DragEnter from *inside* the assignment, and
the area under the cursor at that moment is the view the drag started in. `DragProxy.active` is a
binding on `Drag.active`, and `activeChanged` is emitted *after* that delivery — so in that one
event the property still reads `false`. The handler therefore took the external branch, found no
URLs, and ran `drag.accepted = false`. Qt delivers nothing further to a drop area that rejected the
DragEnter, so the view stayed dead for the rest of the gesture: no highlight, no `onDropped`, and
`Drag.drop()` returning `Qt.IgnoreAction` at the end. The three chrome drop areas never see that
first event, which is why only the two views were affected.

Fixed by guarding on `!drag.hasUrls && dragProxy.sourceNav` in both views' `updateDropTarget()` —
`begin()` assigns `sourceNav` *before* `Drag.active`, so it is the one payload signal already true
in that first event. `FolderTreePanel`/`QuickAccessSection`/`Breadcrumb` keep the `active` guard;
they can't be a gesture's starting point.

Worth remembering generally: **a QML binding on `Drag.active` is not readable from a handler that
the same assignment triggered.** The two `Drag.drop()`-ordering notes in point 3 above are the same
hazard seen from the other end of the gesture.

## Phase 17a — frameless window + own caption row (done)

Windows' native title bar is gone; the row where it used to be is `qml/components/CaptionBar.qml`.
Nothing else moved — the tab strip stayed below in `Main.qml`'s header, which was the point of the
split: 17a is a like-for-like replacement whose success looks like *no visible change*, so the
frameless plumbing could be shaken out before 17b started rearranging things on top of it.

Path 3 of `docs/TITLEBAR_TABS_INVESTIGATION.md` (vendor QWindowKit) rather than a DIY
`FramelessWindowHint`, because everything a hand-rolled version silently loses — the Snap Layout
flyout, DWM minimise/restore animation, drop shadow, Win11 rounded corners, 8-direction resize — is
exactly what QWindowKit's `WM_NCCALCSIZE`/`WM_NCHITTEST` handling keeps. No `#ifdef Q_OS_WIN`
fallback path exists: this app is already MSVC/vcpkg/Windows-only (`WindowsSessionStore`,
`QSettingsPinnedFolderStore`).

### CMake / dependency

`third_party/qwindowkit` is the third submodule, pinned to tag **1.5.0**. Unlike `third_party/sdk`
it is *not* shallow — and it carries its own nested `qmsetup` submodule (which in turn carries
`syscmdline`), so a fresh clone genuinely needs the `--recursive` that `CLAUDE.md` already
prescribes. Four cache variables are forced before `add_subdirectory`: `BUILD_QUICK` on,
`BUILD_WIDGETS` off (this app is `QGuiApplication`, no `QtWidgets`), `BUILD_STATIC` on (so nothing
new has to join the `PATH` dance to run the binary), `INSTALL` off. `find_package(Qt6 ...)` gained
`Gui`; `appMegaExplorer` links `QWindowKit::Quick`.

This was flagged as the phase's largest risk — QWindowKit's root `CMakeLists.txt` configures and
builds `qmsetup` as a *separate* host project at configure time, which looked likely to collide with
this project's "VS generator + vcpkg toolchain" configure. **It didn't.** Neither of the two planned
fallbacks (pre-building QWindowKit and pointing `QWindowKit_DIR` at it; retreating to path 1) was
needed. Configure does log `The CorePrivate target is mentioned as a dependency for
QWindowKit::Quick, but not declared` (and the same for `GuiPrivate`/`QuickPrivate`) — cosmetic, a
known Qt 6.10+ change in how private modules are exported; linking succeeds.

Tag 1.5.0 rather than the newer `master`: pinning to a release keeps the dependency legible. The
cost is two Windows-side fixes that only exist past the tag — `#243` (window drift in the QWK layer)
and `#229` (top border overlap). Neither reproduced in manual testing, and the height drift 17a hit
(below) was ours, fixed on our side.

### QML type registration: `QML_FOREIGN`, not `QWK::registerTypes`

The documented setup is `QWK::registerTypes(&engine)` plus `import QWindowKit 1.0`, but that is a
*runtime* registration: `qmlcachegen` compiling this project's own QML module can't see the type and
warns about an unresolved import. Since `qwkquickglobal.cpp` shows the call is nothing but
`qmlRegisterType<QuickWindowAgent>("QWindowKit", 1, 0, "WindowAgent")`, the type is re-registered
into the `MegaExplorer` module instead, via a header-only `src/qml/WindowAgentForeign.h`
(`Q_GADGET` + `QML_NAMED_ELEMENT(WindowAgent)` + `QML_FOREIGN`) added to `qt_add_qml_module`'s
`SOURCES`. `Main.qml` writes `WindowAgent { }` with no extra import, and both `qmlcachegen` and
`qmllint` see the type. `main.cpp` is untouched — no `registerTypes`, no `rootObjects()`, and
`setup()` is called from QML. `WindowAgentBase`'s `SystemButton` enum resolves at runtime without a
second foreign gadget, so none was added.

### Registration order: why `registerWithAgent()` exists

`QuickWindowAgent::setTitleBar`/`setSystemButton`/`setHitTestVisible` all dereference `d->context`
unguarded, and that context is only created by `setup()`. `Component.onCompleted` fires
innermost-object-first, so the natural spelling — each system button registering itself from its own
`onCompleted` — runs button then `CaptionBar` then window, i.e. **every registration before
`setup()`, and crashes**. Instead `CaptionBar` exposes `registerWithAgent()` and `Main.qml`'s root
`Component.onCompleted` calls `winAgent.setup(window)` and then that, in the one place where the
order is visible.

The agent's id is `winAgent`, not `windowAgent`: QML resolves an identifier against the scope
object's properties before ids, so `CaptionBar { windowAgent: windowAgent }` would bind the property
to itself.

### The window grew 31px per launch

Found while measuring, not while testing — the saved window height climbed 996, 1027, 1058, 1089
across four launches, 31px each time, which is exactly the native caption height. Dropping the
caption hands those pixels to the client area, so `window.height` jumps the moment the window
becomes visible, and `Settings`' `property alias windowHeight: window.height` faithfully persists
the inflated value for the next launch to inflate again.

Probing narrowed it to the assignment itself: `setup()` is harmless, and the growth is **synchronous
with `visible` turning true** (native handle creation), not deferred to a later frame. So the fix is
to be synchronous back — `visible: false` declaratively, then `Component.onCompleted` saves the
restored size, shows the window, and writes the size back. It settles within the same turn, before
the first frame is presented. Verified stable at 1200x800 over four consecutive launches; the test
runs' inflated setting was reset by hand.

Note that the same saved size now yields a window 31px shorter overall than before 17a, since the
caption is part of the client area. That is inherent to the integration, not a bug.

### Deliberately not done

- **macOS/Linux branches.** Windows-only, as above.
- **`Mica`/`Acrylic` backdrop** (`setWindowAttribute("mica", true)`) — matching it to
  `FluentWinUI3`'s palette is its own piece of work.
- **Persisting the maximised state.** `Settings` still stores width/height only, so quitting
  maximised reopens at the normal size.
- **An app icon in the caption.** Title text only.

## Phase 17b — tab strip moved onto the caption row (done)

`TabStrip` now lives inside `CaptionBar`, Explorer-11 style, and `headerComponent` is back to being
the address-bar `ToolBar` directly — the `ColumnLayout` wrapper Phase 9 added to stack the strip
above it had one child left and was dropped.

### QML: caption-row composition

`CaptionBar` is still a plain `Item` (it *is* the registered title bar) with anchor-positioned
children rather than a `RowLayout`, because the tab strip's width is a computed clamp, not a share
of a layout:

    width: max(0, min(tabStrip.preferredWidth, captionWidth - systemButtons.width - dragReserve))

`preferredWidth` is `count * maxTabWidth + addButton.implicitWidth` — what the strip would like once
every tab is at its cap — and `dragReserve` (120px) is the band the strip may never enter. Both
matter for the same reason: **anything the strip covers stops being caption**, so a `fillWidth`
strip would leave a window with open tabs undraggable. The gap the clamp leaves, plus everything
right of the "+" button, is the drag area.

The strip is `visible:` on `authState === LoggedIn` rather than wrapped in a `Loader` — the caption
row itself deliberately sits *outside* that gate (17a), so hiding the strip when logged out simply
hands the whole row back to dragging. The title `Label` is the inverse
(`visible: !tabStrip.visible`), because Explorer 11 shows no title text once tabs are on the caption
row.

### Hit-test registration: two items, not every delegate

Anything on the caption row that should react to clicks must be registered with
`setHitTestVisible()`, or the OS swallows the click as caption (the failure mode is that clicking a
tab drags the window instead). The plan expected per-delegate registration from each `TabButton`'s
`Component.onCompleted`, with `Component.onDestruction` as a possible counterpart. Reading
`AbstractWindowContext::isInTitleBarDraggableArea()` showed neither is needed:

- The test is `mapGeometryToScene(item).contains(pos)` over the registered items — a **rect** test,
  so registering `tabBar` covers every `TabButton` and its close button without the Repeater-created
  delegates knowing anything about the agent. Only `tabBar` and the "+" button are registered.
- The items are held as `QVector<QPointer<QObject>>` and null-checked on every hit test, so
  unregistering on destruction is unnecessary in general — and moot here, since both registered
  items outlive the window.

Registration is driven from `CaptionBar.registerWithAgent()` (see 17a) for the `setup()`-ordering
reason, not from `TabStrip`'s own `onCompleted`.

### `TabBar` ignores implicit widths — tabs must set `width`

The per-tab width clamp is an **explicit `width`** on `TabButton`, the only control in that file
that sets one, and the reason is specific: `QQuickTabBarPrivate::updateLayout()` divides the bar's
width evenly among the tabs whose width is still *implicit*, without consulting what those implicit
widths are. Writing the clamp as `implicitWidth` therefore did nothing — at 15 tabs every tab was
squeezed to ~60px and its label elided away to nothing, leaving a visibly empty strip. Assigning
`width` takes the button out of that redistribution pool entirely (`updateLayout` only reserves its
size), so `clamp(80, 200, tabBar.availableWidth / tabBar.count)` decides, and the bar scrolls
(`clip: true`) once the tabs stop fitting. Caught on-screen with 15 tabs open, not in review.

### Deliberately not done

- **Drag-to-reorder tabs, and tearing a tab off into a new window.** The hardest part of the whole
  idea and in direct conflict with caption dragging. Nothing regresses by leaving it out — neither
  ever existed.
- Everything 17a lists as not done stays not done.

---

## UI polish pass S0–S9 (done) — the C++ half

The UI-tidying stages themselves are logged in `docs/DESIGN_IMPROVEMENT.md` (findings, decisions,
per-stage measurements), not here: they are QML work with no phase-level architecture to record.
Four of them did reach C++, and those belong next to the rest of the interface's history.

**S3** added `MEGAEXPLORER_COLOR_SCHEME=light|dark`, read in `main.cpp` right after
`QGuiApplication` is constructed and applied via `QStyleHints::setColorScheme()` (Qt 6.8+). One call
switches the whole UI, because FluentWinUI3 and `qml/Theme.qml` both read
`Application.styleHints.colorScheme` — style and design tokens cannot disagree. It exists so
light/dark can be checked without touching the Windows personalization setting; the eventual
settings screen replaces where the value comes from, not what it does.

**S5** added `IMegaClient::hasSubfolders(handle, isRoot)` — the **sixth** synchronous method, after
`currentSessionToken` / `currentUserHandle` / `checkMove` / `checkUpload` / `findChildFiles`. Same
justification as the last three (an in-memory query against the node tree already held since
`fetchNodes`, no round-trip), plus one of its own: its caller is
`QAbstractItemModel::hasChildren()`, which answers the view inline and has nowhere to put a
callback.

It exists because `FolderTreeModel::hasChildren()` used to answer "yes" for any node it hadn't
loaded, so **every** folder in the side panel got an expand chevron, childless ones included. The
obvious repair — flip the answer once a load comes back empty — doesn't work: `TreeView`'s internal
proxy is not guaranteed to re-query `hasChildren()` after a plain `dataChanged()`, so the arrow
could linger anyway (Phase 10 recorded this as a known limitation).

`MegaSdkClient` implements it with `MegaApi::getNumChildFolders()` rather than walking
`getChildren()`, which avoids allocating a `MegaNodeList` per query — this runs per visible row on
every layout pass. `FolderTreeService` collapses the `Result<bool>` to the plain bool the model
returns, reading a failure as "no children": a handle that no longer resolves should not carry an
expand arrow. `FileEntry` was deliberately **not** given a `hasSubfolders` field, the other option
on the table — nothing outside the folder tree wants it, and every other producer of a `FileEntry`
would have had to leave it meaningless.

> **TRAP for tests**: `Result<bool>::success` defaults to false (`src/core/Result.h`), so gmock's
> default action for an unstubbed `hasSubfolders()` is a *failure*, which the service reads as "no
> children". `FolderTreeModelTest::SetUp` carries a blanket `EXPECT_CALL` for it — same shape as
> `UploadServiceTest`'s for `checkUpload`, and the same failure mode if it's missing: unrelated
> assertions fail, with nothing pointing at the cause.

**S7** added `FolderNavigationController::goUp()` / `canGoUp` for the address bar's new "up" button
(D4). The alternative — having QML read the second-to-last `breadcrumb` entry and call
`navigateTo()` — was rejected to keep navigation vocabulary in one place: `breadcrumb` is declared
as a *display* structure, and deriving movement from it would make its `"handle"`/`"isRoot"` keys
serve two contracts at once. "Up" pushes onto the back stack like any other navigation, so "back"
undoes it.

**S9** added `FileListModel`'s `count` property — `Q_PROPERTY(int count READ rowCount NOTIFY
countChanged)`, emitted from `setEntries()` right after `endResetModel()`, which is the only place
the row count moves. The inherited `rowCount()` is `Q_INVOKABLE` but carries no NOTIFY, so the
status bar's new item-count binding would have gone stale on the first navigation. Same shape as
`QuickAccessModel::count` and `TabsController::count`. The selection half of that readout needed
nothing new: `selectedHandles` was already a `Q_PROPERTY` with `NOTIFY selectionChanged`.
