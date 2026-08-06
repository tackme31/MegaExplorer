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
| 18 | Login loading screen + SDK cache location | done (pulled forward) |
| 19 | Menu-action redesign + new folder | done (pulled forward) |
| 20a | Per-tab busy indicator + refresh that really refreshes | done (pulled forward) |
| 20b | About / License dialogs | done (pulled forward) |
| 21 | Rubber-band (rectangle) selection | done (pulled forward) |
| 22a | Quick-access reordering | done (pulled forward) |
| 22b | Tab reordering + drop-onto-tab move | done (pulled forward) |
| 23 | Copy / cut / paste | done (pulled forward) |
| 15 | In-app preview (side panel, `getPreview`) | planned |
| 16 | Real-time remote-change reflection | future, post-MVP |
| 24+ | Undecided | undo and full bidirectional local sync both stay out of scope |

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

### Phase 18 — login loading screen + SDK cache location (done)

Ahead of 15/16: the login screen showed **nothing** between submitting the form and the
Cloud Drive appearing (`LoginView.qml` gated its indicator on `authState === Restoring` only), and
on a large account that gap was **measured at 6 minutes 25 seconds**. Self-contained — auth path
plus one composition-root line, shares nothing with preview or remote-change reflection.

Design, measurements and the reasoning behind every decision below live in
`docs/FETCHNODES_PROGRESS_INVESTIGATION.md` (read 追記2 first — it supersedes the earlier
predictions). Do not re-derive them here; this is only the checklist.

- [x] **`basePath` を `AppLocalDataLocation` に固定** (`main.cpp` / `MegaSdkClient.h`, was
      `"."` = the launch CWD). Independent of everything below and the single biggest win: with the
      SDK state-cache DB found, the same account's fetchNodes was **619 ms instead of 384.8 s**.
      A changing CWD silently costs a full re-fetch.
- [x] **`IMegaClient::fetchNodes` に `onProgress(transferred, total)` を追加** — same two-callback
      shape `download`/`upload` already use. Fakes/mocks under `tests/` follow.
- [x] **`AuthService` の 3 メソッド**（`restoreSession`/`login`/`loginWithTwoFactor`）が進捗を
      素通しする。
- [x] **`AuthController` に読み込み段階の状態を持たせる** — stage enum、進捗率、
      整形済みバイト文字列。SDK スレッドからの値は
      `DownloadController.cpp` と同じ `QMetaObject::invokeMethod(qApp, …, Qt::QueuedConnection)`
      で GUI スレッドへ。**「準備」段階は実装時に落とした** — 下記ログ参照。
- [x] **「ダウンロード完了」判定に無更新タイムアウトのフォールバック**を入れる（実測でログに
      残った最後の値は 99.44%。100% ちょうどが観測できる保証がない）。
- [x] **`LoginView.qml` をローディング対応にする** — `Restoring` に加えて
      `LoggingIn`/`VerifyingTwoFactor`/`LoggingOut` でも出す。ダウンロード中のみ確定バー、
      復号中は不定 + 補足文言（**経過時間は実機確認後にユーザー判断で削除**、下記ログ参照）。
- [x] ついでに **S11 の「フォームとインジケータが同じ `ColumnLayout` にあって高さが跳ねる」**
      （`docs/DESIGN_IMPROVEMENT.md` 8 節）を `StackLayout` にして解消。同節 1 番目
      （`"crimson"` ハードコード）も同時に消化した。

やらないと決めたもの: 全体を 1 本の % で見せること（ダウンロードは全体の 42% でしかなく、残り
57% は進捗を出す手段が SDK に無い）、ノード件数の表示（`getNumNodes()` はルート確定まで 0）、
`EVENT_REQSTAT_PROGRESS`（`reqstat` リクエスト自体が失敗しており実質使えない）。

別件として拾ったもの（このフェーズの範囲外）: `MegaApi::logout` は状態キャッシュ DB を破棄する
ので、**サインアウト 1 回のコストが 6 分半**になる。導線に警告を出すかどうかは未決。

### Phase 19 — menu-action redesign + new folder (done)

Ahead of 15/16, same "self-contained" reasoning as 13a/13b/14a/17/18. The trigger was a small
feature request — right-click empty space → "New folder" → name dialog → create — that the existing
menu machinery could not express at all: `fileActionApplies()` returned false for an empty
selection unconditionally, and empty space *is* an empty selection. Rather than bolt a second
mechanism on beside it, the resolver gained the missing dimension (which menu) and all three menus
were moved onto it. See the implementation-log entry below.

### Phases 20a–23 — the detail pass (all pulled forward, ahead of 15)

Same "self-contained" reasoning as 13a/13b/14a/17/18/19, but this time as a block: with the feature
set broadly in place, the remaining rough edges are worth more than the biggest remaining feature.
Phase 15 is the highest-effort item on the whole list and its absence costs the least (download→open
already covers "view the file"), so it moves behind all of these.

Ordered by cost and by which of them touch the same input handling. 20a/20b are additive and land
first; 21 is a self-contained input change; 22a/22b are the drag-and-drop group; 23 is the only one
that adds an SDK-mutating call.

**Undo is deliberately not on this list.** It was considered alongside 23 (they're one request:
copy/cut/paste/undo) and dropped: MEGA has no native undo, so every operation would need a
hand-built inverse (rename↔rename, rubbish→move back, copy→delete, create→delete), a record hook in
every mutating path, and a policy for when the history has to be thrown away. That is a phase in its
own right, and with 23 in place the practical need for it is small. Not deferred — out of scope.

### Phase 20a — per-tab busy indicator + refresh that really refreshes (done)

Swap the tab's folder icon for a spinner while that tab has work in flight, with a short delay
before it appears so a fast operation doesn't flash it.

Planned as "…while that tab's *listing* is being fetched", to close out Phase 7b's stated follow-up
(removing the node cache was said to have put a network round-trip in front of every navigation).
**That premise turned out to be false** and the phase was re-scoped before implementation — see the
log below.

### Phase 20b — About / License dialogs (done)

Two entries in `Main.qml`'s existing "More" menu (currently Sign out only). About: version (from
`PROJECT_VERSION`, passed down as a compile definition) plus a link to the GitHub repo. License:
this app's own license plus third-party notices — Qt, MEGA SDK (BSD-2), QWindowKit (Apache-2.0),
FreeImage, FFmpeg, pdfium and the rest of the vcpkg set. The dialogs are small; the real work is the
notice inventory, and with Qt + FFmpeg in the link line this is a shipping prerequisite rather than
a nicety.

The inventory turned out to be generated rather than written, and came to 36 components — see the
log below.

### Phase 20c — account section in the "More" menu (done)

Unplanned, and numbered after 20b because it lands in the same menu that phase built. Nothing in the
app said *which account is signed in* — a real gap once you keep more than one. Chrome's profile
menu, transplanted: avatar, display name, email, storage bar and plan name, above 20b's two entries
and the existing Sign out.

Everything is fetched lazily on the first open, so the login path gains no request at all; storage
alone is re-read on every open. See the log below for why the menu's own open animation had to go.

### Phase 21 — rubber-band (rectangle) selection (done)

Drag on empty space to select the items the rectangle covers, in both the grid and the list view.
Continues Phase 13a's selection model, which needs a range-by-rows entry point (`selectRow` is
per-row and modifier-driven). The load-bearing decision is where the gesture starts: on empty space
it's a rubber band, on an item it's Phase 14a's move drag. Edge auto-scroll can reuse
`DragAutoScroller`.

Both halves of that decision moved, in the end: the grid's inter-tile gap became empty space (it
wasn't, for drags) and the list's strip right of the last column became empty space too — see the
log below.

### Phase 22a — quick-access reordering (done)

Drag a pin up/down to reorder it. Persistence is already ordered-list-shaped and
`QuickAccessService::replaceAll` already exists, so this is a `QuickAccessModel` move operation plus
the QML gesture. `QuickAccessSection` is also a *drop target* for node moves (14a), so the two drags
have to be told apart — `DragProxy`'s `Drag.keys` is the existing mechanism for exactly that.

Landed differently on that last point: the gesture never starts a Qt drag at all, so there was
nothing to tell apart. See its implementation-log entry.

### Phase 22b — tab reordering + drop-onto-tab move (done)

Two features, one phase, deliberately: both take over pointer input on a `TabButton`, and building
one without the other means rewriting its gesture handling when the second arrives.

- **Reorder**: drag a tab along the strip. `TabsController` is a `QAbstractListModel` and gains a
  move operation; the QML side computes the insertion point itself, since `TabStrip.qml`'s buttons
  carry explicit widths (see its comment on why).
- **Drop onto a tab**: drag nodes over a tab, dwell, and that tab activates — then drop into the
  view as usual. A `DropArea` plus a dwell timer per tab; the move itself is 14a's
  `canDropHandlesOn`/`moveHandlesTo`, and the destination tab's refresh is 14b's `refreshIfShowing`.

Phase 17b listed reordering as not-done because it fights caption dragging. That's the problem to
solve here, and the ground is better than it looks: `tabBar` is already registered hit-test-visible
with QWindowKit, so a drag starting on a tab shouldn't reach the window-move path — to be confirmed
first. Tearing a tab off into a new window stays out of scope.

Confirmed, and so was the bigger unknown: `CROSS_TAB_DND_INVESTIGATION.md`'s one open risk (a tab
switch cancelling the source `DragHandler`'s grab) did not happen, so the `StackLayout` rework it
had lined up as the fix was never needed. See the implementation-log entry.

### Phase 23 — copy / cut / paste (done)

The first *non-move* duplication: `IMegaClient::copyNode` (fifth mutating method), an app-global
clipboard holding node handles plus a copy/cut mode, and paste into the current folder. Cut is
14a's `moveNode` reused, so it costs almost nothing beside copy. Phase 19's `MenuActionResolver` +
`ActionCatalog` split is what makes the three menu entries cheap, and Phase 19's own leftover —
Refresh / Select all / paste on the background menu — is picked up here. Same-name collisions on
paste follow 14b's precedent (server's `API_EEXIST`, plus the replace/skip dialog if warranted).
Ctrl+C/X/V accelerators included.

Done, except that the collision sentence above turned out to be **wrong about the mechanism** —
`copyNode` has no `API_EEXIST`, and a colliding name is worse than untidy. See the
implementation-log entry.

### Phase 23a — Ctrl+drag copies (done)

Unplanned follow-on: 14a/22b's drag & drop could only move, and Phase 23 had just built everything a
copy needs. Ctrl (without Shift) turns the drag into a copy on all six existing drop targets;
Shift is the explicit "move". No new drop target, no new SDK method.

The awkward half is that Qt hands the app nothing to read the modifier from — see the
implementation-log entry, which also records the one Phase 23 behaviour this changed
(copying a folder into its own subtree is now refused on paste too).

### Phase 15 — in-app preview (side panel, `getPreview`/`startStreaming`)

Lowest priority; explicitly deferred in Phase 4 since download→open already covers "view the file".
Format-specific rendering (image/PDF/text/...) makes this the highest-effort item on the list.

### Phase 16 (future) — real-time remote-change reflection

Reflect other devices' changes via the SDK's push-notification mechanism into whatever listing is
open. Additive on top of phase 6's refresh.

### Phase 24+ — undecided

Undo (see the 20a–23 preamble) and full bidirectional local sync both stay out of scope for the
foreseeable future.

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

## Phase 18 — login loading screen + SDK cache location (done)

Two independent changes that happen to share a screen: the SDK state-cache DB now lives at a fixed
path (which removes most of the wait), and the wait that remains is explained instead of shown as a
blank panel. The measurements behind every decision are in
`docs/FETCHNODES_PROGRESS_INVESTIGATION.md`; only what was actually built is here.

### `basePath` — the change that matters most

`main.cpp` constructed `MegaSdkClient` with its default `basePath = "."`, i.e. the launch CWD. The
SDK unconditionally creates its state-cache DB there (`megaapi_impl.cpp`'s `MegaDbAccess`), so
starting the app from Qt Creator and from the exe directly used *different* caches, and a cache miss
means re-downloading the whole node tree: **384.8 s vs 619 ms** on the 640k-node account. It now
gets `AppLocalDataLocation`, the same directory `session.dat` already used.

Three details that are easy to get wrong:

- `QDir().mkpath(cacheDir)` has to run **before** the client is constructed, not after. `MegaApi`'s
  constructor builds the DB layer immediately and sqlite only asserts the path is absolute, never
  that it exists — get the order wrong and it fails silently, with the symptom six minutes away.
- The path is passed through `QDir::toNativeSeparators`. `QStandardPaths` returns forward slashes
  and the SDK appends with backslashes; the mix survives plain Win32 calls but not a `\?\` prefix.
- `MegaSdkClient`'s `basePath` parameter **lost its default value**. There is exactly one caller, and
  a default that silently means "wherever you happened to launch from" is what caused this.

Two consequences worth remembering: existing `megaclient_statecache15_*.db*` files in the repo root
are now orphaned (gitignored, delete at will), and anyone who *did* have a working cache under a
stable CWD pays one full re-fetch on the upgrade.

### Stages: three, not the four that were planned

The plan had a "preparing your account" stage between authentication and download, detected by
having `AuthService` fire the progress callback once with `(0, 0)` before starting the fetch. That
was dropped during implementation: the SDK only calls `setTotalBytes` once the response length is
known (`megaapi_impl.cpp:16150`), so a **genuine** update can also arrive as `(0, 0)` — the marker
was never unique, and the comment describing it as one would have been false. Rather than add a
second callback to three methods, the stage went away: the investigation itself had already judged
that region (1.1 s of account info + 3.0 s to first byte) not worth its own wording.

So `AuthController::LoadingStage` is `Authenticating → DownloadingNodes → DecryptingNodes`, plus
`SigningOut` and `NotLoading`. `AuthService` now adds nothing to the progress stream at all; it
forwards `IMegaClient::fetchNodes`'s two callbacks and that is the whole change there.

### The download → decrypt handover

There is no event marking the end of the download. Byte progress simply stops, and the last value
observed in practice was 99.44%, so waiting for an exact 100% would leave the bar frozen for the
remaining 3.5 minutes. The handover is therefore a quiet-period timeout (`kStallTimeoutMs`, 8 s
against a measured ~1.15 s average interval), and it is **not a one-way latch**: a later progress
event moves the stage back to `DownloadingNodes`. That distinction is the whole design — a genuinely
stalled connection recovers on its own instead of being stuck showing the wrong message, which is
what a "only advance past 90%" threshold would have produced.

What makes the timeout sound rather than a guess: the SDK's `request_response_progress` early-returns
unless the fetchnodes CS request is still pending, so once the response is complete no further update
is structurally possible.

### Two hazards in the plumbing

- **`getTransferredBytes()`/`getTotalBytes()` return `long long` and can still be `-1`.** Casting
  straight to `std::uint64_t` (which is what copying `DownloadListener` verbatim would do) yields
  1.8e19 and pins the bar at zero forever. `FetchNodesListener` clamps both to 0.
- **Stale events across a re-login.** A logout or re-login while a fetch is in flight leaves queued
  progress events behind. `AuthController` stamps each attempt with an `mLoadGeneration` counter that
  the progress lambda captures; mismatches are dropped.

Loading state is reset from inside `setState()` rather than at each call site, deriving the stage
from the new `AuthState`. Five terminal paths never reach `fetchNodes` at all (wrong password,
`kEMfaRequired`, wrong 2FA code, `cancelTwoFactor`, a transient restore failure) and any of them
could otherwise have left a timer running.

`fetchProgress` (`qreal`) and `fetchProgressText` (`QString`) mirror `DownloadController`'s
`activeProgress`: QML has no `formattedDataSize` equivalent, so the "3.1 MB / 183.3 MB" string is
built in C++ with `QLocale::system().formattedDataSize(..., DataSizeTraditionalFormat)`, same as
`FileListModel`'s size column.

### `LoginView.qml`: StackLayout, and the S11 pickup

The three pages (credentials / 2FA / loading) are a `StackLayout` because its implicit size is the
maximum over **all** children, which is exactly the fix S11 wanted for the height jumping on every
state change. Three things this refactor depends on:

- Pages must **not** carry `visible:` bindings of their own — `StackLayout` drives that property
  imperatively and the two fight.
- `StackLayout` children default `Layout.fillWidth`/`fillHeight` to **true**, unlike every other
  layout. Each page pads itself with `Layout.fillHeight` spacers to stay vertically centred, and the
  credentials fields now stretch to the full 320px (a deliberate visual change).
- The `BusyIndicator` is gated on `StackLayout.isCurrentItem`, not just `visible`. Whether the style
  stops animating a hidden indicator is style-private, and `LoginView` is never destroyed while
  logged out, so a stuck animation would drive the render loop for the entire session.

The now-redundant `enabled:` bindings on the form fields went away with it: the page switch is the
single gate, and two gates that have to agree is the worse design. S11's `color: "crimson"` →
`Theme.color.danger` was picked up here too; the rest of S11 (app name/logo, field labels,
section 9's responsive work) is untouched.

### Deliberately not done

- **No cancel.** Once a first sign-in starts the user is committed for the duration, and
  `session.dat` is only written inside the `fetchNodes` success callback — so killing the app
  mid-fetch costs both the session *and* the state-cache DB, i.e. the full wait again next launch.
  The copy on screen deliberately does not imply that closing is safe.
- **No "later sign-ins are fast" promise.** That holds only while the state-cache DB survives, and
  `MegaApi::logout` destroys it. The decrypt-stage text says "This can take a few minutes the first
  time you sign in." and stops there.
- **No `AuthControllerTest`.** `src/qml` controllers are untested by convention; the stage machine
  was verified on the real account instead. `AuthServiceTest` gained one test asserting that
  `AuthService` forwards the progress stream untouched.
- **No elapsed-time readout during decryption.** It was built and shipped in the first cut, then
  removed after the user tried it: an animating indicator already says "still working", and a
  counter next to it is one more number to read for no decision it helps make. The `QTimer` and the
  `decryptElapsedSeconds` property went with it rather than staying unused.

> **All timings quoted here are from a Debug build.** The 218 s tail is CPU-bound decrypt/tree-build
> work, so a Release build could move the 42:57 split noticeably. The design doesn't depend on that
> ratio, but the "a few minutes" wording should be re-checked once there is a Release measurement.

## Phase 19 — menu-action redesign + new folder (done)

Two things in one phase, because the second couldn't be built on the first as it stood.
"Right-click empty space → New folder" needs a menu whose target is *not* the selection, and
`fileActionApplies()` opened with an unconditional `if (selection.total() == 0) return false;`. The
resolver had no way to say "this action applies when nothing is selected".

### Why not a second hardcoded menu beside the resolver

That was the cheap option, and `FolderPinMenu.qml` showed what it costs: it was already a hardcoded
menu built beside the table, and its own header comment instructed the reader to keep its item
order "in step with `FileActionResolver`'s `defaultFileActions()`" by hand. Its two labels were
duplicated from `FileContextMenu.qml`'s `actionLabels` map as well. A third such menu would have
made that three places to synchronize, right before a run of small features (refresh, select all,
copy/paste, properties, share link) that are almost entirely menu items spanning several menus.

### Why not per-case static arrays either

The other obvious shape — one static array per (menu x selection shape) — was considered and
rejected on counting. Of the menu sites that exist, only *one* has combinatorics: the file views'
selection menu, where the target varies over files/folders x single/multi. The empty-space menu,
the tree row and the quick-access pin row all have a fixed target of exactly one folder. Per-case
arrays would have meant ~6 arrays for the one site that needs them, scattering "rename is
single-selection only" across all of them, while adding nothing for the three sites that are
already just a list.

### The split that was adopted

Three concerns, previously entangled, now separate:

| concern | where | grows by |
|---|---|---|
| which actions a site offers, and the global display order | `src/core/MenuActionResolver` | one table row per action |
| whether an action applies to the current state | same file, one predicate | one predicate per action |
| label, greying, execution | `qml/ActionCatalog.qml` | one entry per action |

`MenuSite` (`FileSelection`/`FolderBackground`/`FolderRow`) is the new dimension, and it decides
**membership only** — ordering stays global, in `defaultMenuActions()`. That is what fixes
`FolderPinMenu`'s hand-synchronization structurally: an action shared by two sites cannot come out
in a different order in one of them, because there is only one order.
`MenuActionResolverTest.SharedActionsKeepTheSameRelativeOrderAcrossSites` pins it.

`FileAction`/`FileActionResolver` were renamed to `MenuAction`/`MenuActionResolver` — the actions
are no longer all about files, and the pre-release refactor allowance made the rename cheaper than
the misleading name.

Execution deliberately stayed in QML. Every target (`downloadController`, `tabsController`,
`quickAccessModel`, and each view's inline rename field / dialogs) is a QML-side object, so a C++
command object would have had to call back into QML for all of them. Not only taste: the
pre-existing fact that `TogglePin` couldn't resolve its own pinned/unpinned label in C++ is a
layering constraint — `src/core` cannot see `src/qml`'s `QuickAccessModel` — not an oversight.

The two fixed-target sites have no model to hang a property off, so they read their IDs from a new
`MenuActions` singleton (`src/qml`, `QML_ELEMENT` + `QML_SINGLETON`, the codebase's first real QML
singleton in C++). It deliberately exposes *only* those two sites: `FileSelection` still goes
through `FileListModel::availableActions`, which has the change signal, and offering it on the
singleton would have silently answered "as if one folder were selected".

### `folderTargetContext()`: the trick that kept the predicates unchanged

The fixed-target sites synthesize `SelectionSummary{fileCount: 0, folderCount: 1}`. One folder
satisfies `FoldersOnly` and `SingleOnly` by construction, so `ActionTarget`/`ActionArity` work at
those sites untouched — and the old "empty selection matches nothing" short circuit could stay
exactly as it was, since a synthesized context is never empty. The entire resolver change is one
`siteMatches()` call plus the `sites` field.

### The duplicate-name check is the server's, and it is the only check

`MegaApi::createFolder` returns `API_EEXIST` when the parent already holds a same-named folder
(documented in `megaapi.h`), so no `findChildFolders`-style synchronous helper was added — it would
have been both redundant and raceable against another device. `MegaErrorCodes.h` gained `kEExist`,
with the matching `static_assert` in `MegaSdkClient.cpp` as always. `Result<void>` was kept even
though the SDK does return the new handle: nothing needs it, and it keeps `createFolder` on the
shared self-deleting `SimpleResultListener`, so no new listener class was written.

Note MEGA lets a file and a folder share a name, so an existing *file* of that name is not a
conflict — the same asymmetry `UploadService`'s `findChildFiles` relies on from the other side.

### Failures split by whose problem they are

`FolderNavigationController::createFolder` reports through two channels at once, chosen by error
code:

- `kEExist` / `kEArgs` → `folderCreationFailed("exists"/"invalidName")` only, no toast. The user is
  looking at the dialog they can fix it in, so the dialog stays open with the name still in the
  field and says so in red.
- anything else → `notifyError("createFolder", …)` plus `folderCreationFailed("other")`, and the
  dialog closes, because the toast is now carrying the message.
- success → `refreshVisibleListing()`, `notifyOperation("createFolder", 1, 0)`, `folderCreated()`.

The reason is a structured selector, not a sentence — same rule as `NotificationController`'s
context strings. `ToastStack.qml` needed both a `showOperation` and a `showError` case; an unknown
`showOperation` context is dropped silently, so forgetting it would have made success invisible
rather than noisy.

### `standardButtons` can't keep a dialog open

`NewFolderDialog.qml` is the first dialog here that has to survive its own Ok: a taken name must
leave the dialog open, text intact, showing the error. `Dialog`'s standard Ok accepts and closes
unconditionally, so the button box is hand-built with Ok on `DialogButtonBox.ActionRole` — the same
escape hatch `Main.qml`'s `nameConflictDialog` already used for its three-way choice. A `busy` flag
disables the field and both buttons across the round trip; without it a second Ok would create two
folders.

It lives in `TabContentPane.qml`, one per **tab**, not per view like `ConfirmRubbishDialog.qml`:
the result arrives as a signal from the tab's `FolderNavigationController`, which the list and grid
views share, so a per-view instance would have had both copies reacting to every result.

### The empty-space right-click fires twice

The delegates' existing right-button `TapHandler`s use the default `DragThreshold` gesture policy,
which takes only a passive grab — so a new viewport-level handler fires *in addition to* them, on
delegates as well as on empty space. Both views therefore hit-test first and bail out when the tap
landed on a row, reusing the same `rowAt()` / `indexAtViewportPos()` helpers drag & drop uses (the
grid's already insets by half the tile gap, so the space *between* tiles counts as empty). Same
`parent: tableView` / `parent: root` re-parenting as the left-button handler, for the same reason:
a plain child of a `Flickable` lands in `contentItem`, which is only as tall as the content and so
never sees taps below the last row.

### Deliberately not done

- Other tabs showing the same folder, and the side-panel folder tree, are not refreshed — same
  split as Phase 14a's move. `refreshIfShowing` exists and `UploadController::destinationChanged`
  is the fan-out pattern, but generalizing it belongs with Phase 16's remote-change reflection
  rather than being half-built here.
- No in-memory pre-check of the name while typing.
- The new folder is not selected, scrolled to, or opened after creation; `createFolder` returns
  `Result<void>` accordingly.
- The background menu holds only "New folder". Refresh / Select all / paste are now one catalog
  entry plus one table row each, and are left for the next small feature.
- The tab strip's own right-click menu is out of scope: its vocabulary is tabs, not nodes.
- `FolderNavigationController` gained no test, keeping that file's stated "bent for exactly one
  thing" convention intact; the branch logic is covered one layer down in
  `FileOperationServiceTest`.

## Phase 20a — per-tab busy indicator + refresh that really refreshes (done)

### The premise this phase was planned on was wrong

Phase 7b's log (and the roadmap entry it fed) says removing the node cache "put a network latency in
front of every navigation" that a busy indicator should cover. It doesn't. `IMegaClient.h` states it
outright for the four listing getters — "Synchronous under the hood, but kept callback-shaped for
interface consistency" — and `MegaSdkClient.cpp` bears it out: `search()` calls `mApi->search(...)`
and then `onDone(...)` on the very next line, `getPath()` walks parents in a loop and calls `onDone`
at the end. The SDK holds the entire node tree in memory after `fetchNodes`, so `getChildren` is a
lookup, not a request.

So navigation, search, sorting and breadcrumb resolution all block the GUI thread and finish inside
one event-loop turn. A spinner for them could not have painted even once — the blocking happens
inside the synchronous call, before anything gets a chance to repaint. Had this phase been built as
written, it would have shipped a control that is never visible.

What actually has latency is the `MegaRequestListener`-backed half of `IMegaClient`: `renameNode`,
`createFolder`, `moveNode`, `moveToRubbish`, `upload`, `getThumbnail`. So the indicator covers
mutations instead of listings, and the property is called `busy`, not `loading`.

### `refresh()` was covering for the same misconception

The toolbar refresh button and F5 went through the same in-memory path, which commit 46bdb7e already
recorded when it added them ("a re-read of the SDK's already-fetched node tree, not a server
round-trip"). It re-reads whatever the SDK happens to have been told already and guarantees nothing
about freshness — the one action whose entire purpose is freshness.

`MegaApi::catchup()` is the fix: a real request whose completion the SDK documents as "the SDK is
guaranteed to be up to date (as for the time this function is called)". It becomes
`IMegaClient::syncPendingChanges` (implemented in one line over the existing `SimpleResultListener`)
and a passthrough `FolderNavigationService::syncWithServer` — routed through that service rather
than injected as a fourth dependency into `FolderNavigationController`, since it is the only one of
the controller's three services holding an `IMegaClient`. `refresh()` now syncs, then re-reads.

That also gives the spinner a second, more frequent occasion to appear, which is what made the two
halves worth doing in one phase.

**A failed sync still re-reads the listing**, and toasts under a new `"refresh"` context. Withholding
what the SDK already has would punish the user for the network being down, in response to them asking
to see the folder.

`refreshIfShowing` was split off rather than left delegating to `refresh()`. Its callers
(`UploadController::destinationChanged` → `TabContentPane.qml`) are reporting a change *this app just
made*, which the SDK necessarily already knows about, and it fires once per tab showing the
destination — so delegating would have meant N pointless round-trips per upload. Both now share a new
private `refreshListingIfLoaded()` (the old `refresh()` body, `mHasLoadedOnce` guard included).

### Counting, not flagging

`busy` is backed by an `int mBusyCount`, not a bool: `moveSelectionToRubbish`/`moveHandlesTo` fan out
to N independent SDK calls, and the tab is busy until the last one lands. Two private choke points,
`beginBusyOperation`/`endBusyOperation`, are the only places that touch it — one pair per SDK call,
never per user gesture.

Two traps the code comments call out:

- **`endBusyOperation` goes at the very top of each callback, above its own branching.** `createFolder`
  has four outcomes (success, `kEExist`, `kEArgs`, other) and each of them returns; decrementing per
  branch would have been four chances to leak the count.
- **`reset()` zeroes the count while operations are still in flight** (logout), so those callbacks
  arrive with nothing left to subtract. `endBusyOperation` clamps at zero rather than going negative
  — a negative count would stop any later `beginBusyOperation` from ever reaching 1 again, silently
  killing the indicator for the rest of the session.

The published value is `mBusyVisible`, which only turns true once a 250ms single-shot `QTimer` fires
— the "short delay" the phase called for, and the reason a fast operation shows nothing at all.
Structured after `AuthController`'s `mStallTimer` (timer by value, configured in the constructor,
interval as a documented `constexpr` in the anonymous namespace).

Deliberately **no** minimum-visible duration: an operation finishing just past 250ms will flash the
spinner briefly. That's a second timer and a second piece of state for a problem not yet observed;
if it shows up in practice it can be added then.

### QML: a fixed box, for the same reason the close button sits outside `contentItem`

`TabsController` gained a `BusyRole`, and the row-lookup-by-pointer loop that `breadcrumbChanged`
used moved into a private `emitRowChangedFor(navigation, roles)` now that two signals need it.

In `TabStrip.qml` the bare `FileIcon` became an `Item` pinned to `Theme.iconSize.sm` holding both the
icon and a `BusyIndicator`, one visible at a time. The pinning is load-bearing and is the same hazard
the file already documents for its close button: `TabButton` takes its `implicitHeight` from
`contentItem`, and `BusyIndicator`'s own implicit size is the style's ~32px, which would have grown
every tab from 36px. `running` is bound to `busy` rather than left default, per the lesson
`LoginView.qml` records — whether a style stops animating a hidden indicator is style-private, and a
stuck one drives the render loop for as long as the tab lives.

`FileIcon.qml` was left alone. A `loading` property there would have appeared on all four other call
sites (tree panel, quick access, grid, table) that can never use it.

### Uploads: the one mutation with no owning tab

Not in the plan, added straight after it on the observation that the destination tab sat still
through an upload — the only *long* mutation in the app, and the one where a spinner is worth most.
It couldn't go through `beginBusyOperation` like the other four, because `UploadController` is
app-global by design (three of the five drop targets are shared chrome, see its class comment) and so
has no tab to charge the operation to. Pushing a begin/end pair into "whichever tabs are showing the
destination" would also leak: a tab that navigates away mid-upload would never receive its `end`.

So it's folded in one level up instead, as a pure function of state rather than a counter:
`TabsController::data(BusyRole)` returns `navigation->busy() || uploads->isUploadingTo(currentHandle,
atRoot)`. Nothing to pair, nothing to leak — a tab that navigates away simply stops matching, and one
that navigates *in* starts. That second direction is why `breadcrumbChanged` now invalidates
`BusyRole` too, alongside the new whole-column invalidation on
`UploadController::activeDestinationsChanged`.

`UploadController` answers `isUploadingTo` from a new `pendingByDestination` map, counted at
*enqueue* time. The existing `Batch::destinations` set couldn't be reused for this: it is
success-only and filled when a job *lands*, because it drives the refresh fan-out — a spinner has to
go up when the work is queued, and come down whether the upload succeeded or not. A replace hands its
release over to the Rubbish-bin move that follows it, so the destination keeps spinning until the old
node is actually gone (matching the batch flush, which waits for the same thing).

Consciously left inconsistent with the other four: **no 250ms delay** on this path. The delay exists
to hide sub-perceptual operations, and a network file transfer is never one.

### Tests

`tests/FolderNavigationControllerTest.cpp` bends the "src/qml is untested by convention" rule for
bookkeeping, which is exactly what the busy count is. Six tests added: the sync-before-re-read
ordering, the failed sync still re-reading, `refreshIfShowing` not syncing, the count surviving a
bulk fan-out, a failing `createFolder` still clearing it, and `reset()` mid-flight followed by a
fresh operation that must still show the indicator.

Two things worth knowing before touching this file:

- **A new pure virtual on `IMegaClient` silently breaks existing tests.** gMock's default for an
  unstubbed void method is to do nothing, so `refresh()` handed its callback to a mock that dropped
  it and the re-read never happened. `SetUp` now carries a `WillRepeatedly` default for
  `syncPendingChanges`; tests wanting a failure set their own, which gMock matches first.
- **No `QSignalSpy`** — it lives in Qt6::Test, which this target deliberately doesn't link (the same
  note is in `UploadControllerTest`). The busy tests have to let real time pass for the 250ms timer,
  so there's a local `waitForBusy()` helper built from `QEventLoop` + `QTimer` instead.

`tests/UploadControllerTest.cpp` covers the retain/release pairing with three more: busy from enqueue
until the queue drains (and for that destination only), still cleared when every job fails, and still
set while a replaced node is being binned. `TabsControllerTest` now builds a real `UploadController`
to satisfy the new constructor argument — with nothing enqueued its answer is a constant false, which
leaves those tests testing row bookkeeping exactly as before.

### Deliberately not done

- Making the listing getters genuinely async. A recursive `search()` over a large account still
  blocks the GUI thread, and no indicator fixes that — it needs the SDK calls off the GUI thread,
  which is Phase 16's territory.
- Thumbnail loading is not counted. Each tile has its own placeholder; a whole-tab spinner would be
  the wrong granularity.

## Phase 20b — About / License dialogs (done)

Two entries in the "More" menu, backed by an inventory of every third-party component the binary
carries, with each component's full license text embedded in the executable.

### Why the text has to be *in* the product, not linked from it

The question the phase opened with was whether third-party licenses need to be user-visible at all.
The answer that shaped the design: what the licenses actually require is not visibility but
*accompaniment*. BSD-3 §2 wants the notice reproduced "in the documentation and/or other materials
provided with the distribution", Apache-2.0 §4(a) wants recipients given "a copy of this License",
GPLv3 §4 the same. A URL is not a copy, so the fact that this app is useless offline — the obvious
argument for linking instead of embedding — turns out to be beside the point.

That still leaves the choice of *where* the copy lives, and a `THIRD-PARTY-NOTICES.txt` next to the
executable would satisfy it. It is embedded in the binary as well because a user copying just the
`.exe` out of the archive is normal behaviour, and the notices would silently come off. Two things
genuinely want to be in the running program rather than in a file: Qt's requirement that the user be
told Qt is used under the (L)GPL, and FFmpeg's requirement (ffmpeg.org/legal.html) of a specific
sentence in the about box. Links are used for the one thing licenses do accept a link for — where to
get the source (GPLv3 §6 allows a network server).

### The inventory is generated, and that decision has consequences

`scripts/gen_third_party_notices.py` is the repo's first script and produces four committed
artifacts from one pass: `licenses/manifest.json`, `licenses/texts/<id>.txt` (36 files),
`licenses/licenses.cmake`, and a re-rendered `THIRD-PARTY-NOTICES.txt`. The dialog and the flat file
therefore cannot disagree — they are the same data rendered twice.

**It is run by hand, not at build time**, for two reasons that are worth keeping:

- `qt_add_qml_module`'s `RESOURCES` list has to be known at configure time. A build-time generator
  would mean listing files that do not exist yet, or a `file(GLOB)` that goes stale until the next
  reconfigure.
- The input is `build/msvc-debug/vcpkg_installed/`, which is under `.gitignore`'s `/build*/` and is
  absent on a clean clone *and* at configure time. A build-time generator would quietly emit a
  binary with no notices in it — the worst available failure mode for this particular feature.

Committing the output also means a dependency bump shows up as "these license texts changed" in
`git diff`, which is where a human is supposed to look at it. `--check` re-renders and exits
non-zero on any difference, including a leftover text file for a dropped dependency; there is no CI
to run it from yet.

The bulk of the input is free: vcpkg materializes `share/<port>/copyright` and `vcpkg.spdx.json` for
every port. Only directories carrying an spdx file are walked — the rest (`WebP`, `jpeg`, `png`,
`lcms2`, `unofficial-*`) are aliases that would otherwise be counted twice.

### What needed human judgement anyway

Seven ports could not be taken at face value, and the reasoning lives in `LICENSE_OVERRIDES` next to
each entry:

| port | vcpkg says | recorded as | why |
|---|---|---|---|
| ffmpeg | `LicenseRef-vcpkg-null` | LGPL-2.1-or-later | copyright file is the LGPL-2.1 text; the port's `gpl` feature is not requested |
| jasper | `LicenseRef-vcpkg-null` | JasPer-2.0 | named in the copyright file's first line |
| liblzma | `LicenseRef-vcpkg-null` | 0BSD | XZ Utils 5.8's core |
| freeimage | GPL-2.0 OR GPL-3.0 OR FreeImage | FreeImage (FIPL) | §3.6 lets the executable ship under another license; also the text the port's `copyright` actually carries |
| zstd | BSD-3-Clause OR GPL-2.0-only | BSD-3-Clause | permissive side |
| freetype | FTL OR GPL-2.0-or-later | FTL | permissive side |
| libraw | LGPL-2.1-only OR CDDL-1.0 | LGPL-2.1-only | the LGPL side; statically linked, so §6's relinking is met by publishing this app's source |

`gtest` is excluded (test-only, never in the distribution). Of the SDK's seven vendored libraries
only five are listed: `third_party/sdk/third_party/CMakeLists.txt` gates `evt-tls` behind
`USE_LIBUV` (OFF here) and `glob` behind `NOT WIN32`, so neither is linked and neither is
distributed. `glob` ships no license file at all, which would have to be chased upstream if it ever
became reachable.

Qt is the one entry whose text is hand-written: it is an external install with no license file in
the repo, and its own bundled third-party set is far too large to reproduce, so the entry carries
the (L)GPL pointer plus the URL to Qt's published list.

### `LicenseModel` and why the text is not a role

`src/qml/LicenseModel` is a `QAbstractListModel` reading the manifest from
`:/qt/qml/MegaExplorer/licenses/`, and the codebase's second true QML singleton — same test as
`MenuActions`: no injected dependencies, unchanging for the process lifetime, so `main.cpp` needs no
wiring. A missing or malformed manifest logs and yields an empty model rather than asserting.

The license *text* is deliberately `Q_INVOKABLE licenseText(int)` and not a role. The texts total
several hundred KB, and a role would pull every one of them in the moment the view instantiates its
delegates. QML calls it once per selection change, and the results are cached in a `mutable QHash`.

### The blank-pane bug, which cost most of the QML time

Switching components after scrolling painted an empty right-hand pane. Three fixes were tried and
failed before the cause was found, so the sequence is worth recording:

1. `ScrollBar.vertical.position = 0` after assigning the text — no effect.
2. An explicit `Flickable` instead of `ScrollView`, so `contentY` could be set unambiguously — no
   effect.
3. Resetting `contentY` *before* the assignment as well as after — no effect.

Temporary logging then showed the state was entirely correct at the moment of failure: `contentY` 0,
`contentHeight` 306, `implicitHeight` 306, the text 883 characters. Nothing was mis-scrolled; the
text simply was not being painted, and no amount of subsequent scrolling brought it back. The cause
is `TextEdit`'s viewport optimization — it builds scene-graph nodes only for the stretch of text
near its enclosing flickable's viewport, and replacing a scrolled 34KB document with a short one in
a single pass leaves that node range where the old document had it.

The fix is three ordered steps in `showLicense()`: clear the text (dropping every node), reset
`contentY` against the now-empty document, and assign the real text a frame later via
`Qt.callLater`. All three are load-bearing; the comment there says so.

The `TextArea`'s own background is suppressed and the frame drawn by a parent `Rectangle` — a
background inside the flickable scrolls away with the text.

### Version wiring

`project()` gained its missing PATCH component (`0.1` → `0.1.0`) and the repo's first
`target_compile_definitions` passes `MEGAEXPLORER_VERSION` to `main.cpp`, which forwards it to
`QCoreApplication::setApplicationVersion`. QML reads `Qt.application.version`. No `AppInfo` type was
introduced: Qt already owns a path for exactly this, and the only other thing About might want —
the Qt version — is already a row in the license list.

### Tests

`tests/LicenseModelTest.cpp` reads the working copy's `licenses/` through a second, test-only
constructor (`MEGAEXPLORER_LICENSE_DIR`); `MegaExplorerTests` embeds no qrc, and the generated files
are the subject anyway. It guards only invariants that compile cleanly, stay invisible until someone
opens the dialog, and are compliance problems when they break: row 0 is this app, **every** row's
text is non-empty (a manifest entry whose file is missing is also missing from the qrc), names are
unique, no row's license id still contains `LicenseRef-`, out-of-range rows return empty, and a
missing manifest yields an empty model instead of a crash.

The generator has no unit tests — there is no Python test infrastructure here and `--check` covers
the same ground more cheaply.

### Follow-up: shipping the notices alongside the binary

**Deliberately out of scope, and the real remaining obligation.** `LICENSE` and
`THIRD-PARTY-NOTICES.txt` are not yet copied to the build/install output. What the licenses require
is accompaniment of the *distribution*; the in-binary copy is belt-and-braces on top of that, not a
substitute for it. This wants an `install(FILES ...)` and a decision about what the distributable
archive actually looks like, neither of which exists yet.

Also left:

- Qt's own bundled third-party components (harfbuzz, pcre2, …) are covered by a pointer to
  doc.qt.io rather than enumerated. Qt ships `sbom/*.spdx.json` if that is ever wanted properly.
- FFmpeg under the LGPL requires that the user be able to relink against a modified FFmpeg. Shipping
  it as DLLs satisfies this in practice, but it is not written down anywhere.
- `gen_third_party_notices.py --check` should run in CI. There is no CI.

### Deliberately not done

- No search/filter in the license list. 36 rows fit in one scroll.
- The About dialog's text is hand-written in QML rather than generated from `ABOUT.txt`: it needs
  `qsTr()`, clickable links, and the version interpolated. `ABOUT.txt` was rewritten by hand to stop
  claiming Qt and the MEGA SDK are the only dependencies, and now points at the generated file.
- No "copy all" button. The text is selectable, and the flat file exists.

### Addendum 2026-08-07 — relicensed GPLv3 → MIT

The inventory this phase produced made it checkable that **nothing in the dependency set forces
copyleft**, which is what the GPLv3 choice had assumed. Qt's essentials (QtQuick, Controls +
FluentWinUI3, Layouts, Effects, QtCore, Window) are all available under LGPLv3, and `freeimage`'s
vcpkg port is `GPL-2.0 OR GPL-3.0 OR FreeImage` — the GPL side had been picked only "to match this
app". Everything else was already permissive or weak copyleft. So the app's own code is now MIT.

What that took, beyond swapping `LICENSE` for the MIT text:

- **Qt is declared under LGPLv3, not GPLv3.** No build change — Qt was always dynamically linked —
  but LGPLv3 is written as additional permissions on top of GPLv3, so both texts have to ship. They
  used to ride along in the app's own GPLv3 `LICENSE`; now `licenses/upstream/{LGPL,GPL}-3.0.txt`
  are inputs to the generator and `qt_text()` appends both to the Qt entry. The old `QT_NOTICE`
  pointed at *"the MegaExplorer entry of this document"* for the GPLv3 text, which a MIT relicense
  would have left dangling.
- **`freeimage` → `FreeImage` (FIPL).** FIPL §3.6 explicitly allows the executable form under a
  different license. This also fixed an existing inconsistency: `licenses/texts/freeimage.txt` was
  already the FIPL text while the manifest declared GPL-3.0.
- **LibRaw is statically linked** (the SDK's overlay triplet builds everything but ffmpeg static),
  so LGPL-2.1 §6's relinking requirement is real. It is met by the app's source being published
  under MIT, and `NOTICES_PREAMBLE` now says so explicitly instead of leaving it implied — which
  also closes the "not written down anywhere" item above, for LibRaw as well as FFmpeg. The
  practical consequence, worth remembering: **this app cannot be taken closed-source** without
  making LibRaw dynamic first.
- `NOTICES_PREAMBLE` gained the LGPL-source-availability section and the notice FIPL §3.6 requires;
  it lost the GPLv3 §6 corresponding-source and §15/§16 warranty paragraphs, which MIT's own text
  covers.
- `AboutDialog.qml` says MIT and "Qt … under the LGPLv3"; the FFmpeg sentence is prescribed by
  ffmpeg.org and was left byte-identical.

## Phase 21 — rubber-band (rectangle) selection (done)

Press on empty space in either file view and drag: a rectangle follows the pointer and selects
everything it covers. Ctrl adds to the selection that was already there; without it the band
replaces it. Dragging to the top/bottom edge auto-scrolls, and the rectangle keeps growing over the
rows that scroll into it.

### The gesture had a free slot, and one that only looked free

Phase 14a set `Flickable.acceptedButtons: Qt.NoButton` on both views so a left-drag would start a
move instead of panning. That is also what left a press-drag on empty space unclaimed: nothing had
to be taken away from Flickable to add this, and the two gestures can't collide because one starts
on an item and the other doesn't.

"On an item" was the part that needed work. Since S8a the grid's hit test (`indexAtViewportPos`)
deliberately returns -1 inside the 8px gap ring around a tile, so tap, hover and drop all agree that
the gutter is empty space. The delegate's `DragHandler` never got that memo: it sat on the delegate
`Item`, which is the whole *cell*, gap included, so a drag starting in the gutter picked up the
neighbouring tile — and selected it first on the way. Fixing it is one line (`parent: tile`, pointing
the handler at the inset `Rectangle` S8 introduced), and it makes the band's start rule and the
view's own hit test the same rule.

The list view's mirror case went the other way. The strip right of the last column belongs to the
row for clicks — `rowAt()` clamps x into the last column on purpose (Explorer's full-row hit area,
S6a) — but it carries no delegate and no `DragHandler`, so a press-drag there meant nothing at all.
It became band space rather than growing a move drag: the strip is visually empty, and it is the one
place in the list where a band can be started without landing on a row first.

### `BandSelector.qml` owns the gesture, the views own the geometry

The split is: the component knows about pointers, coordinates and auto-scroll; the host view knows
what its items are. Two required properties carry the second half — `isOnItem(viewPos)` for the
start rule, and the host's handler for `bandChanged(contentRect)`, which turns a rectangle into
rows. Nothing about tiles or columns leaks into the component.

Three details are load-bearing:

- **The origin is kept in content coordinates, the pointer in view coordinates.** Auto-scroll moves
  the content under a stationary pointer; if both ends were in view coordinates the band would slide
  with the view instead of growing. The mapping is written out as `pointerView + contentX/Y` rather
  than `contentItem.mapFromItem()` for a binding reason: the arithmetic form re-evaluates when
  `contentX`/`contentY` change, so one `onContentRectChanged` handler covers pointer moves and
  scroll steps alike, and the rectangle and the selection can never disagree.
- **`parent: root.view` on both the `DragHandler` and the rectangle.** The same idiom the views'
  own view-level handlers and `DropArea`s already spell out: an `Item` child of a `Flickable` lands
  in `contentItem`, which scrolls and is only as tall as the content, so it would never see a press
  below the last row. As a side effect the rectangle becomes a sibling of `contentItem` created
  after it, which is exactly the stacking it needs — above the delegates.
- **The start rule is checked on activation, not bound.** A `DragHandler` can't decline a grab it
  has already taken, so when the press turns out to be on an item (or the view is renaming) the
  handler simply stays inert for the rest of the gesture. In practice the delegate's own handler
  took the exclusive grab long before this runs — a `DragHandler` can't take over from another one
  of the same type — so this is the second line of defence, not the first.

### Rows are computed, not hit-tested

Neither view's existing hit test can answer "which rows does this rectangle cover".
`indexAtViewportPos` needs `itemAtIndex()` and `rowAt()` needs `cellAtPosition()`, and both only
resolve *realized* delegates — but a band that auto-scrolls asks about rows that were never
realized. So both views do the arithmetic instead:

- Grid: `columns = floor(width / cellWidth)` (the same expression `Keys.onPressed` uses, recomputed
  rather than cached for the same reason), tiles inset by `gap / 2`, and the band's row/column range
  solved from that. The assumption that comes with dropping `itemAtIndex()` is that the cell grid
  starts at content (0, 0) — true for a `GridView` with no header, whose margins move the scroll
  range rather than the first cell. `indexAtViewportPos` avoids that assumption on purpose; here it
  is stated instead.
- List: uniform 32px rows (`Theme.rowHeight.normal`, the delegate's `implicitHeight` and the only
  thing that sets a row's height), first row at content y 0 — the header is a separate view above
  the `TableView`, not a header row inside it. x is not consulted at all, matching `rowAt()`'s clamp.

### The model: a session, not a range call

`FileListModel` gained `beginBandSelection(additive)` / `updateBandSelection*` /
`endBandSelection()` / `cancelBandSelection()`. The session exists for two reasons. Ctrl+band is a
union with *the selection as it was at press time*, so that set has to be captured once and kept
(`mBandBase`) rather than re-derived from a selection the band itself keeps rewriting. And the band
is recomputed from scratch on every update, which is what makes shrinking the rectangle deselect
again — `selectRow`'s Shift branch, the only pre-existing range code, always cleared first and could
only ever grow a replacement.

Two update overloads rather than one: the list passes a contiguous `(firstRow, lastRow)`, the grid a
block `(firstGridRow, lastGridRow, columns, firstColumn, lastColumn)`. A grid band is not a
contiguous run of model rows — a tall narrow rectangle over one column selects every *n*th row — and
the alternative, a row *list*, would mean marshalling thousands of ints per mouse move. The block
form is O(covered) and clamps the grid row against the entry count *before* multiplying, so a band
dragged far past the end can't overflow the index.

`endBandSelection()` puts the anchor on the band's first covered row and the cursor on its last, so
a Shift+click straight after a band extends from where the band started — the same relationship a
click leaves behind. An empty band clears both, which is the drag equivalent of clicking empty
space.

`notifySelectionChanged()` gained a row-range overload, and the band uses it. The existing one emits
`dataChanged` over the whole table, which is fine for a click but means a full-table repaint per
frame during a drag; the band's diff pass already knows which rows flipped, so the range comes for
free. If nothing flipped, nothing is emitted at all — a band dragged through empty space below the
last row is silent.

`setEntries()` drops any live session (the rows it was measured against are gone), next to the
`pruneSelection()` call that was already there.

### Testing

11 new `FileListModelTest` cases, all pure C++: replace vs. additive, shrinking, the empty band, the
grid block including the partial last row and the past-the-end clamp, anchor/cursor after the end
(asserted through a following Shift+click rather than by reading them back), cancel, updates
arriving after the session ended, `setEntries` during a band, and the `dataChanged` range. 260 tests
pass.

The gesture itself was checked in the running app with the `ui-style` skill's `drive`: a band in the
grid's empty space, one started in the inter-tile gutter (which before this phase would have started
a move drag), and in the list one from the trailing strip and one from below the last row. Ctrl+band
and edge auto-scroll are the two paths `drive` can't reach — it has no key-hold primitive, and the
folder used for the check fit on one screen.

### Deliberately not done

- **Horizontal auto-scroll.** `DragAutoScroller` writes `contentY` only, so a list whose columns
  overflow won't scroll sideways during a band. Adding it would change Phase 14a's drop-drag
  behaviour too, which is a separate decision.
- **Esc to cancel a band.** `cancelBandSelection()` exists and is wired to the handler's
  `onCanceled`, but nothing presses it from the keyboard — that needs the handler to give up its
  grab on demand.
- **Bands in the side panel.** The folder tree and quick-access list are single-selection.
- **Touch.** The handler is `Qt.LeftButton`-only.

## Phase 22a — quick-access pin reordering (done)

Drag a pinned folder up or down in the side panel to change its order. The dragged pin's name
follows the cursor as a ghost, a 2px accent line shows where it will land, and the row itself stays
put until the drop.

### The roadmap's premise didn't survive contact

The plan was to reuse `DragProxy` with a second `Drag.keys` value, so the five existing node-move
`DropArea`s could tell a pin-reorder drag from 14a's node drag. That turned out to solve a problem
this gesture doesn't have: reordering never leaves `pinList`, so it doesn't need to be a Qt drag in
the first place. Not starting one means no `DragEnter` ever reaches the folder tree, the breadcrumb,
the two file views or the pin rows themselves, and **not one line of 14a/14b's accept logic was
touched**. The new key would only have created work — every one of those `DropArea`s would have had
to learn to ignore it.

So the gesture is a `DragHandler` on the pin delegate with `target: null`, and the insertion point
is computed by arithmetic (`Math.round((viewY + contentY) / Theme.rowHeight.compact)`) rather than
by `itemAt`. Same reasoning as Phase 21's band selector: an auto-scrolling list outruns delegate
realization, and rounding rather than flooring is what puts the boundary at each row's midpoint —
which is what makes the line land *between* rows instead of on one.

**`xAxis.enabled: false` is a trap, and it shipped in the first cut.** It reads like the obvious
constraint for a one-column list, and the docs describe it as being about *dragging range* ("if
enabled is true, horizontal dragging is allowed"), which sounds like it only bounds what gets
applied to `target`. It also gates **activation**: with x disabled, a sideways drag never takes the
exclusive grab, so the ghost doesn't appear at all — and after a sideways excursion the pointer then
has to travel the drag threshold *vertically* before anything happens, which reads as a broken
gesture. The handler's `onGrabChanged` log is what settled it: a horizontal drag went
`GrabPassive` → `UngrabPassive` with no `GrabExclusive` in between, while a vertical one took the
exclusive grab 13px in. Both axes are now left enabled and the logic simply ignores x; a horizontal
drag becomes a reorder that resolves to the row it started on, i.e. a no-op.

Worth recording how *not* to check this: an isolated `qmltestrunner` harness first said horizontal
activation worked fine with x disabled — because `xAxis.enabled` was being bound to a property that
evaluated to `undefined` in the delegate, leaving it at its default of `true`. QML printed
`Unable to assign [undefined] to bool` and the harness happily measured the wrong configuration. The
running app plus `console.warn` in `onGrabChanged` was both faster and correct.

`pinList` also had to give up the left button (`acceptedButtons: Qt.NoButton`, the Flickable
property `FolderTreePanel.qml` already uses): without it the list's own click-drag panning steals
the gesture the moment the list is tall enough to scroll. Wheel scrolling is unaffected, so the
existing `interactive: contentHeight > height` rule stands.

### `DragProxy` gained a ghost-only mode

The ghost is worth reusing — it's the same "what am I dragging" affordance — but the payload and
`Drag.active` are not. So `DragProxy` grew `ghostOnly` plus `beginGhost()`/`finishGhost()`:
`visible` becomes `Drag.active || ghostOnly`, `moveTo()` stops early-returning when only the ghost
is live, and `active`/`sourceNav`/`handles` are left strictly alone. Every existing drop target
branches on `dragProxy.active` (or, in the two views, `sourceNav`), so all of them stay blind to a
reorder by construction rather than by a new rule.

The insertion line is `parent: pinList`, not a plain child — an `Item` declared inside a `Flickable`
lands in its `contentItem` and would scroll away with the rows, the trap `BandSelector.qml` already
documents. Its `y` is additionally clamped to `pinList.height - height`: the "after the last pin"
insertion point lands exactly on the bottom edge, where the list's own `clip` swallowed the line
entirely (caught in the running app, not in review).

### C++: one service method, one model method — and a sweep that had to be rewritten

`QuickAccessService::move(from, to)` is a `std::rotate` plus the same write-through every other
mutator does, with `to` defined as a **final position** rather than an insertion point in the old
coordinates. `QuickAccessModel::move(handle, toRow)` is handle-keyed like everything else in that
class, clamps `toRow` into range (so a drag released below the last row means "last"), and announces
the change with `beginMoveRows`/`endMoveRows` — a move, not a remove-plus-insert, so the view keeps
its delegates. `beginMoveRows`' destination is an insertion point in the *pre-move* coordinates, so
moving down has to name `to + 1`; that off-by-one is the only subtle part.

The login-time validation sweep then had to change, and this is the part that wasn't in the original
sketch. It used to commit **the snapshot it started from**, in that snapshot's order — so a reorder
landing mid-sweep would be silently undone (as would a `pin()`; the same latent bug predates this
phase). It now walks `mService->pins()` — the *current* list — and uses the sweep only as a
handle-keyed source of refreshed names and drops; a handle the sweep never saw passes through
unchecked. The dropped-pin marker changed with it: zeroing the handle made the pin unfindable by
handle, so `Sweep` carries a parallel `std::vector<bool> usable` instead.

Bumping `mGeneration` in `move()` would have been a one-liner alternative, and was rejected:
`activate()`'s callbacks read the same guard, so a drag started right after a click would swallow
that click's navigation.

### Testing

5 new service/model cases (reorder down and up with the exact persisted vectors, out-of-range and
no-op rejection without a save, `rowsMoved` emitted once, unknown handle ignored, past-the-end
clamped to the last row) plus one for the sweep rework — a reorder committed between `reload()` and
the resolve callbacks must survive them. 266 tests pass.

The gesture was checked in the running app with `ui-style`'s `drive`: dragging the first pin below
the last reorders it, a plain click straight afterwards still navigates (the exclusive grab cancels
the `ItemDelegate`'s pending click, so nothing extra was needed for that), and dragging it back
restores the order. The ghost and the insertion line can't be captured — `drive` has no
press/release primitive, so there's no way to hold a drag open across a screenshot.

### Deliberately not done

- **Reorder from the keyboard or the context menu.** "Move up"/"Move down" in `FolderPinMenu` was
  considered and dropped: `MenuSite::FolderRow` is shared with the folder tree's rows, so the two
  items would need a "only when this is a pin row" rule threaded through `MenuActionResolver` and
  `ActionCatalog` — a bigger change than the gesture itself, for a menu Explorer doesn't have either.
- **Dragging a pin out of the panel.** Still not a thing: a pin is a shortcut, and unpinning stays
  in the right-click menu.
- **Reordering the folder tree.** It reflects the real node tree; there's no order to own.

---

## Phase 22b — tab reordering + drop-onto-tab move (done)

Drag a tab along the strip to change its order, and drag nodes (or files from Explorer) onto a tab
to switch to it or drop straight into what it is showing. Both take over pointer input on the same
`TabButton`, which is why they are one phase.

### The investigation's one open risk didn't fire

`docs/CROSS_TAB_DND_INVESTIGATION.md` called exactly one thing unproven: switching tabs mid-drag
makes the source pane `visible: false`, and Qt takes the pointer grab away from items that go
invisible — which would cancel the `DragHandler` holding the drag and make the whole gesture
evaporate the instant a spring-loaded tab fires. It lined up a fix (replace `Main.qml`'s
`StackLayout` with a hand-managed `Item` stack that keeps the drag-source pane visible) and
estimated it as the phase's medium-sized item.

Checked first, in the running app: **the drag survives.** A `DragHandler`'s grabber is the handler
object, not the item it lives on, and that turns out to be enough — `Main.qml:779-849` is untouched,
and so are the focus-handoff and `SplitView.fillWidth` details that would have had to move with it.
The investigation's contingency B is left written down but unused.

The same pre-flight settled a second unknown the roadmap had not flagged at all: whether
`beginMoveRows` survives the trip through **two** `Repeater`s — `TabStrip.qml`'s inside a `TabBar`
(a `Container`, which positions by `contentModel` order rather than child order) and `Main.qml`'s
pane `Repeater`. Both handle it, and the panes are *moved, not rebuilt*. Proved by giving one tab a
grid view and another a list view, then reordering the grid one: it stayed grid, which a recreated
`TabContentPane` could not have done (its `Component.onCompleted` copies `window.viewMode`, which by
then said list). `window.currentPane`'s `Binding` (`Main.qml:843-848`, keyed on
`currentIndex`/`count`) re-evaluates correctly for the same reason the fixup below exists: every
move that changes *which* pane sits at `currentIndex` also changes `currentIndex` itself.

### Reorder: Phase 22a's gesture, rotated 90°

`QuickAccessSection.qml`'s pin reorder ported almost line for line, because it is the same problem:
a `DragHandler` with `target: null` (the tab never leaves its slot), the ghost borrowed from
`DragProxy`'s `ghostOnly` mode, an insertion point computed by arithmetic rather than by `itemAt`,
and a 2px accent line for the boundary. Not one line of the five existing `DropArea`s' accept logic
changed, for 22a's reason: the gesture never starts a Qt drag, so nothing downstream can see it.

Three things had to be re-derived for the horizontal case:

- **The tab pitch had to become a property.** It was written inline in `TabButton.width`
  (Phase 17b's explicit-width workaround for `TabBar::updateLayout`), and the arithmetic needs it as
  a number, not as a property of whichever delegate happens to be under the cursor. Hoisted to
  `TabStrip.root.tabWidth`. Every tab is that wide, which is what makes
  `Math.round((viewX + contentX) / tabWidth)` legal at all.
- **22a's `xAxis.enabled` trap mirrors into `yAxis.enabled`.** Writing `yAxis.enabled: false` on a
  one-row strip reads like the obvious constraint and would break activation the same way — both
  axes stay live, y is ignored, and a vertical drag resolves to a no-op. Recorded in the code, not
  just here.
- **The insertion line can't be declared inside the `TabBar`.** `TabBar` is a `Container`, so an
  `Item` declared in it lands in `contentModel` and *becomes a tab*. It is declared in the root
  `RowLayout` and reparented (`parent: tabBar`) — to the bar, not to its `contentItem`, which is the
  Flickable trap `BandSelector.qml` documents. `x` is clamped at both ends, 22a's bottom-edge fix in
  both directions.

`TabBar`'s `contentItem` is a Fluent-supplied `ListView` with `interactive` at its default, so its
click-drag panning would steal the gesture the moment the tabs stop fitting — 22a's
`acceptedButtons: Qt.NoButton` rule, except the item belongs to the style and can only be reached
through a `Binding`. `interactive: false` was rejected: it would take wheel scrolling with it.

`DragAutoScroller` grew a `horizontal` flag rather than a twin. The strip really does scroll (min tab
width 80, and `CaptionBar` caps how wide the strip may get), and the four existing vertical call
sites are untouched by the default.

### C++: one model method, and a fan-out that is really Phase 14a's debt

`TabsController::moveTab(from, to)` is `QuickAccessModel::move`'s shape — `std::rotate`,
`beginMoveRows` with the `to > from ? to + 1 : to` destination fixup, `to` defined as a final
position rather than an insertion point, no `countChanged`. What it adds is the `currentIndex`
follow: the active tab keeps its *identity*, so dragging another tab across it slides it one slot the
other way. Four cases, the same shape as `closeTab`'s clamping, emitted after `endMoveRows` so
`Main.qml`'s `StackLayout` never sees a new index against the old row order.

The drop half needed no new drop-target machinery at all — `canDropHandlesOn`/`moveHandlesTo`/
`canUploadTo`/`dropUrls` are called exactly as the other five targets call them, with the tab's own
`FolderNavigationController` reached through the model's existing `"navigation"` role (a
`required property var navigation` on the `Repeater` delegate; zero C++). What it did need was
**Phase 14a's known limitation finally paid off**: `moveHandlesTo` refreshes only the tab it was
called on, which was invisible before and is glaring now — drop onto another tab and the file
doesn't appear in the listing sitting right there. So `FolderNavigationController` gained a
`nodesMoved(destination, destinationIsRoot, source, sourceIsRoot)` signal, emitted from a new
`BulkOperationBatch::onComplete` hook once the batch empties and at least one node landed, and
`TabsController::createTab` fans `refreshIfShowing` out to every *other* tab for both ends. Both
ends, because a move empties one folder and fills another. The folder tree still isn't refreshed —
that stays Phase 16's.

### Spring-loaded tabs

A `DropArea` per tab plus a 600ms `Timer`, and the one non-obvious rule is that the timer is armed in
`onEntered` and **never restarted from `onPositionChanged`**. An internal drag only delivers events
while the pointer moves (`DragProxy.qml:17-19`), so restarting on every position update would
postpone the switch for as long as the user keeps moving and then never fire once they stop. "600ms
after entering" is Explorer's rule anyway.

It is also armed independently of whether the drop would be *accepted*: a tab's own current folder is
frequently a bad destination (dragging within one tab, or onto a folder's own parent) while a
subfolder of it is exactly where the user is heading. Only the already-current tab is excluded.

Dropping on the tab button itself moves into that tab's current folder rather than being a
switch-only waypoint — it shortens the common case to one gesture, and the accept test was already
sitting there. External `text/uri-list` drops take the same two paths (hover to switch, or drop to
upload), so the tab strip is the sixth member of 14b's drop-target set and reuses its dialogs
unchanged.

### Testing

8 new `TabsControllerTest` cases: reorder right and left with the exact resulting order, `rowsMoved`
emitted once, no-op and out-of-range rejected without a signal, the `currentIndex` follow in all four
shapes (the active tab dragged, another tab dragged across it in each direction, a move entirely
beside it emitting no `currentTabChanged`), and no `countChanged`. 274 tests pass.

The gesture itself was checked in the running app, partly with `ui-style`'s `drive` (open three tabs,
drag the first to the end — the order changes, the active tab follows, the panes keep their per-tab
view modes) and partly by hand, because `drive` still has no press/release primitive and so cannot
hold a drag open across a screenshot: the spring-loaded switch, the ghost, the insertion line, the
accept outline, the auto-scroll at 10+ tabs and the cross-tab refresh were all verified by driving
the real mouse.

### Deliberately not done

- **Tearing a tab off into a new window.** Out of scope since Phase 17b and still is — it needs a
  second window, a way to hand a `TabContext` between two `TabsController`s, and a drag that leaves
  the window entirely.
- **Reordering from the keyboard or a menu.** Same reasoning as 22a's: Explorer has neither.
- **Dropping a *tab* onto anything.** The reorder never leaves the strip, by construction.
- **Refreshing the folder tree after a cross-tab move.** The other half of Phase 14a's limitation,
  still Phase 16's.

## Phase 23 — copy / cut / paste (done)

An app-global clipboard (`src/qml/ClipboardController`), `IMegaClient::copyNode` as the fifth
mutating method, and one new per-tab entry point, `FolderNavigationController::paste()`. Cut really
is 14a's `moveNode` reused, as the roadmap predicted. Everything else about the roadmap's collision
plan was wrong, and that is the substance of this phase.

### The roadmap's `API_EEXIST` premise was false — and the truth is worse

The roadmap said same-name collisions on paste would follow 14b's precedent: let the server answer
with `API_EEXIST`, raise a replace/skip dialog if warranted. Checking the SDK before planning
(`third_party/sdk/include/megaapi.h:14495` and `:14519`) showed `copyNode` documents no `API_EEXIST`
at all — only `createFolder` does (`:14169`). MEGA permits duplicate sibling names, so the expected
signal simply does not exist.

Reading the implementation turned that from "no signal" into a data-loss hazard.
`megaapi_impl.cpp:21843-21847` (`copyTreeFromOwnedNode`):

```cpp
if (std::shared_ptr<Node> ovn = (node->type == FILENODE) ? client->getovnode(target.get(), &sname) : nullptr)
{
    ovhandle = ovn->nodeHandle();
    fileAlreadyExisted = node->isvalid && ovn->isvalid && node->EqualExceptValidFlag(*ovn);
}
```

A copy of a **file** into a folder that already holds a same-named file does not land beside it — it
attaches as a **new version over** it. And if the two are byte-identical, `performRequest_copy`
converts the resulting `API_EEXIST` into `API_OK` with **no new node created at all**
(`megaapi_impl.cpp:21762-21771`). Folders are unaffected: `getovnode` is `FILENODE`-only, so two
same-named folders coexist.

So the auto-rename this phase implements ("report.pdf" → "report - Copy.pdf" → "report - Copy
(2).pdf") is not an Explorer affectation, it is the only thing standing between a paste and an
unasked-for overwrite. `IMegaClient::copyNode`'s doc comment says so, since a future caller passing
an empty `newName` would silently reintroduce the hazard. Consequences that fall out of it:

- **The destination's names are re-read on every paste**, via
  `FolderNavigationService::refreshCurrent` — deliberately *not* the cached `mLastFolderEntries`,
  because a stale set is exactly what lets a copy version-over. That read is also correct while a
  search is showing, since `refreshCurrent` touches neither the back-stack nor the current location;
  it is the same method `refreshVisibleListing` already leans on for that reason.
- **Names chosen mid-batch are claimed locally.** MEGA allows duplicate siblings, so two clipboard
  entries really can share a name, and neither may be handed the name the other just took.
- `FileOperationService::uniqueCopyName` is a static pure function (like `isValidName`), splitting
  the extension at the last dot for files only — `archive.tar.gz` → `archive.tar - Copy.gz`,
  `My.Folder` → `My.Folder - Copy`, `.gitignore` → `.gitignore - Copy`. The suffix is English rather
  than translated: `src/core` is Qt-free and out of reach of any `.ts` file, and threading a format
  string down from QML costs more than the wart.
- `MegaSdkClient::copyNode` branches on `newName.empty()` between the SDK's two overloads. Not
  stylistic: the named one rejects an empty string with `API_EARGS`
  (`megaapi_impl.cpp:21645-21649`), so "keep the source's name" has to go through the unnamed one.

No replace/skip dialog was built. With auto-rename there is nothing to ask about, and the 14b dialog
exists for a different problem — an upload genuinely cannot keep both files under one name.

### The clipboard holds a source folder, which forced 14a's move open

`ClipboardController` is pure state: entries (`{handle, name, isFolder}`, straight from
`FileListModel::selectedEntries()`), a cut flag, and the folder they were taken from. No services,
no SDK, no operation — pasting belongs to `FolderNavigationController`, which already owns the bulk
fan-out, the busy counter, the refresh and the toast. It is app-global for the obvious reason (cut
in one tab, paste in another), and injected into every per-tab controller as a non-owning pointer,
the same arrangement `NotificationController` has.

Recording the *source folder* is what the drag path never needed. `moveHandlesTo` captured
`currentHandle()`/`atRoot()` at call time, which is right for a drag (the dragging tab is the
source) and wrong for a cut-paste (the source is wherever the clipboard was filled — possibly
another tab, possibly a folder this tab has since left). Its body moved into a private
`moveHandlesFrom(handles, target, targetIsRoot, source, sourceIsRoot)`; `moveHandlesTo` is now one
line over it. Without that split `nodesMoved` would report a source folder the nodes never came
from, and the tab they were cut from would never refresh. Both halves have a regression test.

`nodesCopied(destination, destinationIsRoot)` is the copy counterpart, fanned out by
`TabsController` to the other tabs — one end only, since a copy leaves the source folder untouched.
The folder tree still isn't refreshed; that stays Phase 16's, for copies as much as for moves.

### Where paste says nothing, and where it speaks

Three refusals, deliberately split:

| case | behaviour |
|---|---|
| empty clipboard, or paste before the first listing loaded | silent |
| a cut going back into its own folder | silent, and the menu entry is greyed |
| destination refuses new children (read-only share, folder gone) | `notifyError("paste")` toast |

The middle one is why `ClipboardController::canPasteInto` exists at all: `checkMove` refuses a move
into the node's current parent with `kEArgs` (`MegaSdkClient.cpp:625`, stricter than the SDK on
purpose), so letting the batch through would report N failures for a gesture that means nothing.
A *copy* back into its own folder is allowed — that is the auto-rename case, and the common one.
The third gets a toast because, unlike the other two, the user has no way to guess why nothing
happened. Per-item failures never reach it; they land in the `"copy"`/`"move"` tally.

Two-stage busy accounting: one begin/end pair for the destination read, then N for the N copies. The
count legitimately dips to zero between stages, which restarts the 250ms delay timer and is
invisible — the read is an in-memory `getChildren`. Wrapping the whole paste in an umbrella pair
would have broken the one-pair-per-SDK-call invariant the class documents.

The queued GUI-thread hop around the copy callbacks looks redundant (the callbacks that matter are
already on the GUI thread) but is load-bearing: `MegaSdkClient::copyNode` calls `onDone` *inline*
when `resolveNode` fails, i.e. re-entrantly inside the issuing loop. Without the hop a batch of
already-deleted nodes could drive `remaining` to zero while the loop was still issuing calls.

### Menus, keys, and the ghosting

Five rows joined `defaultMenuActions()` — `Cut`/`Copy` at `FileSelection`,
`Paste`/`SelectAll`/`Refresh` at `FolderBackground`, the last two being Phase 19's own leftover.
Phase 19's claim that each new action costs "one catalog entry plus one table row" held exactly: no
new menu component, no new site, and `FileListModel::availableActions` picked Cut/Copy up for free.
The three background rows are `FoldersOnly`/`SingleOnly` like `NewFolder`, satisfied by construction
by `folderTargetContext`'s synthesized selection — `menuActionApplies` rejects an empty selection
outright, which is why a background action can't be spelled `Any`/`Any`.

Paste's greying is `ActionCatalog`'s `enabled`, sampled at menu-open time from
`FolderNavigationController::canPaste()`. Applicability (C++) cannot see the clipboard, and the menu
is closed whenever the answer could change — the same split `TogglePin`'s root case uses.

Accelerators are `Keys.onPressed` branches in both views, not a window-level `Shortcut`, for the
reason `Main.qml`'s F5 shortcut already records: a `Shortcut` ignores focus and would steal Ctrl+C
from the header search field. **Placement within the handler is load-bearing**: `StandardKey.Cut` is
Ctrl+X *and* Shift+Delete on Windows, and the existing `Qt.Key_Delete` branch tests no modifiers, so
the new branches go *after* it or Shift+Delete silently stops opening the Rubbish-bin confirmation.
The existing `renamingHandle !== 0` guard gave the inline-rename case for free — while the field is
up the view stands down and Ctrl+C/X/V edit its text.

Cut items are ghosted at `Theme.opacity.cut` by binding the delegate to
`clipboardController.cutHandles` — a `QVariantList` property, not an `isCut(handle)` method: a
method call reads no property, so the binding would never re-evaluate. No model role and no
`dataChanged` were needed, and it works in every tab at once. The opacity goes on the *content*, not
the delegate root: in the table on the `RowLayout` (so the selection fill, the trailing row band and
the drop outline stay solid, and the rename `Loader` sibling is untouched), in the grid on the
thumbnail frame and the name label individually (the rename `Loader` shares their `ColumnLayout`).

### Testing

`tests/ClipboardControllerTest.cpp` is new — 11 cases over the state machine, including the two the
sentinel convention makes easy to get wrong: a cut from the root refuses a paste into the root but
allows one into *handle 0, non-root*, which is a different folder. `FolderNavigationControllerTest`
gained 14, most of them about the copy path's two stages (the destination is read before any copy
goes out; only the colliding entry is renamed; two same-named entries get different names; a failed
read falls back to the cache) plus the two `moveHandlesFrom` regressions. `FileOperationServiceTest`
gained the `uniqueCopyName` table and the `copy`/`canAddChildren` pass-throughs.

One fixture trap worth repeating from `UploadControllerTest`: `Result<void>::success` defaults to
**false**, so an unstubbed `checkUpload` makes paste *refuse* rather than merely do nothing. The
default stub sits in `SetUp` next to `syncPendingChanges`'.

Three existing suites needed updating for the new table rows: `MenuActionResolverTest`'s
exact-position assertions, `FileListModelTest`'s two `availableActions` lists, and both controller
fixtures for the new constructor parameter. 318 tests pass; `appMegaExplorer` builds `/W4`-clean.

### Deliberately not done

- **No OS-clipboard interop.** Ctrl+C/X/V move MEGA node handles inside the app; copying files in
  Explorer and pasting here would be an upload, which is 14b's drag & drop path and a separate
  decision.
- **Paste targets the current folder only** — no "paste into folder" on a folder row, the tree or a
  quick-access pin, unlike 14a's five drop targets. The menu row would have to answer "can I paste
  into *that* folder" per row, and drag & drop already covers the same intent.
- **No undo**, per the Phases 20a–23 block note: it was considered as part of this request and is
  out of scope, not deferred.
- **The folder tree is still not refreshed** after a paste that moves or creates a folder — Phase
  16's, same as for 14a's moves.

## Phase 23a — Ctrl+drag copies (done)

Ctrl (without Shift) makes a drag a copy instead of a move, on all six of Phase 14a/22b's drop
targets. Shift is the explicit "move" and wins over Ctrl, matching Explorer. Almost all of the
machinery already existed — `IMegaClient::copyNode`, `FileOperationService::uniqueCopyName`, the
bulk fan-out, `nodesCopied` and its `TabsController` fan-out are Phase 23's, unchanged. What this
phase is really about is the one thing Qt does not provide.

### Qt tells you nothing about the modifier, and the obvious workarounds all fail

The gesture the request named first is "drag, then press Ctrl, then drop" — i.e. the modifier
changes while the pointer is **stationary**. Three sources were considered and two rejected:

- QML's `DragEvent` has no modifier at all (`accepted`/`action`/`keys`/`source`/`x`/`y`, verified
  against Qt 6.11's docs). Dead end, not a matter of reading it more carefully.
- `DragHandler.centroid.modifiers` exists (`BandSelector.qml:102` uses it) but only updates when a
  point event is delivered, i.e. when the pointer moves. Exactly the wrong case.
- `Keys.onPressed`/`onReleased` on the two views would see the key, but is focus-dependent — and
  Phase 22b's spring-loaded tab switch moves focus *during* a drag, so the one gesture that most
  needs it is the one that breaks it.

So `src/qml/KeyboardState.h` (header-only, `QML_ELEMENT` + `QML_SINGLETON`, the same shape as
`WindowAgentForeign.h`) exposes `QGuiApplication::queryKeyboardModifiers()`. Not
`keyboardModifiers()` — that returns the state cached from the last delivered event, which is
precisely the stale value being worked around. It is a `Q_INVOKABLE` method rather than a
`Q_PROPERTY` because there is no change signal it could honestly emit.

`DragProxy` samples it at three moments and polls it at a fourth:

| when | why |
|---|---|
| `begin()`, **before** `Drag.active = true` | the source view's own DragEnter is delivered from inside that assignment (the reason `sourceNav` is already assigned first, `FileGridView.qml:329-343`), and the Timer — bound to `active` — has not started |
| `moveTo()` | the per-pointer-move hook already exists; makes the moving case exact for free |
| `finish()`, **before** `Drag.drop()` | the modifier at button-release is what decides, Explorer's rule. This is what demotes the 100ms Timer to purely cosmetic: the *decision* never depends on the polling interval |
| a 100ms `Timer` while `active` | the only thing covering a stationary pointer; keeps the badge and the highlighting honest between moves |

### The nudge that doesn't work, and what replaced it

A modifier change has to make the hovered `DropArea` re-ask, and four of the six targets computed
`accepting` in `onEntered` only. The tempting fix — nudge `DragProxy.x` by a pixel and back, since
an internal drag emits its events from the attached item's position changes — **does not work**, and
the reason is worth recording. `QQuickDragAttachedPrivate` carries `itemMoved`/`eventQueued` flags:
`itemGeometryChanged()` sets `itemMoved` and posts a single coalesced `QEvent::User`, so the second
geometry change is swallowed and the delivery is *asynchronous*. That is fatal at the drop instant,
where `finish()` re-samples and calls `Drag.drop()` in one JS turn.

Making `accepting` a binding on `containsDrag && dragProxy.canDropOn(...)` fails differently, and
more quietly: `QQuickDropArea` sets `containsDrag` **after** emitting `entered()`, so the binding
would read `false` exactly when every target's external-URL branch assigns
`drag.accepted = accepting` — silently killing drag & drop *uploads* on all six targets.

What works is a `Connections { target: dragProxy; function onCopyModeChanged() }` per target,
guarded on `containsDrag`. A property write fires the signal synchronously in the same JS turn, it
touches neither `drag.accepted` nor `TabStrip`'s `dwellTimer`, and it relies on no Qt internals. No
`onPositionChanged` handler was added anywhere — the four per-delegate targets keep their
"recomputed on enter only" optimisation, which was never wrong, just incomplete.

### The dispatch lives in DragProxy, so the six targets got shorter

`DragProxy` gained `canDropOn(handle, isRoot)` and `dropOn(handle, isRoot)`, which branch on
`copyMode` internally. Every target's internal branch is now one call instead of a three-line
`sourceNav.canDropHandlesOn(handles, ...)`, and the mode is invisible to them. The two file views
also grew a `lastDragPos` and an `updateNodeDropTarget()` split out of `updateDropTarget(drag)`,
since their hit test needs a position and a modifier change supplies no event to read one from.

The payload changed shape with it: `DragProxy.entries` now holds the `{handle, name, isFolder}` maps
(a copy needs the *names*, for `uniqueCopyName`), and `handles` became
`readonly property var handles: root.entries.map(e => e.handle)` so the two cannot drift.
`beginDrag()` in both views already had `selectedEntries()` and was mapping it down to handles, so
this deleted code rather than adding it.

### What "can I copy here" actually asks

There is no `checkCopy` — not in `IMegaClient`, not in the SDK. `FileOperationService::canCopy`
reuses `checkMove` and reinterprets exactly one code: `kEArgs` ("already in that folder") is a
refusal for a move and a legitimate request for a copy, which lands a `... - Copy` sibling.
`kECircular` / `kENoEnt` / `kEAccess` pass through. Two visible consequences, both matching
Explorer and both now recorded in the comments they contradict:

- Ctrl+dragging inside the folder being shown lights the whole viewport frame, and dropping
  duplicates the selection in place.
- `Breadcrumb.qml`'s **last** segment — documented since 14a as deliberately never highlighting for
  a move — highlights for a copy.

`kEAccess` is borrowed slightly too eagerly (`checkMove` also refuses when the *source* can't be
removed, which a copy doesn't need). Unreachable while the app only browses the Cloud Drive; noted
in the header rather than worked around.

`copy()` is now gated on `canCopy()` the same way `move()` is gated on `canMove()`, so skipping the
hover-time pre-check can't smuggle a folder into its own subtree. That gate is also what broke 12
existing tests the moment it landed: `Result<void>::success` defaults to **false**, so an unstubbed
`checkMove` refuses every copy before it reaches `copyNode`. The fixture stub sits next to Phase
23's `checkUpload` one, whose comment already warned about exactly this trap.

**This changed Phase 23's paste**, deliberately: `canPaste()` and `paste()` now run the same
per-entry `canCopy()` check, so pasting a folder into its own subtree is greyed out and refused
rather than attempted. Previously it was allowed and MEGA would snapshot-duplicate the whole tree.

### A drop target is not the folder you're standing in

`paste()` reads the destination's names with `refreshCurrent`, which only works because a paste
always targets the current folder. A drop doesn't, so `FolderNavigationService` gained
`listChildrenOf(handle, isRoot, order, onDone)` — the `navigateTo` branch without the back-stack
push or the `mCurrent` commit.

The two callers diverge on a failed read, and that divergence is the point. `paste()` keeps falling
back to `mLastFolderEntries`, which is correct because the destination *is* the folder this tab is
showing. `copyEntriesTo()` has no listing of some other folder and **refuses the whole drop** — this
is correct rather than merely cautious, since `getChildren` is an in-memory read whose only
realistic failure is a destination that no longer exists. The genuinely tempting wrong answer is an
empty `taken` set: `uniqueCopyName` would then return the source name unchanged and the copy would
version over an existing file (`IMegaClient::copyNode`).

`startCopyBatch` was reduced to a pure fan-out taking `(entries, target, targetIsRoot, taken)`, so
building `taken` — the only thing the two paths disagree about — belongs to the caller.

`BulkOperationBatch` gained an optional `refresh` override. `accountForBulkOutcome` unconditionally
called `refreshVisibleListing()`, which for a drag-copy onto *another* folder is a full model reset
(plus a re-run of the recursive search, if one is showing) of a listing nothing changed. Both copy
paths now set it to `refreshIfShowing(target, targetIsRoot)` — equivalent for a paste, a no-op for a
cross-folder drop, and every other tab is still reached by `nodesCopied`.

### Testing

`FileOperationServiceTest` gained the `canCopy` code table and the `copy()` gate; three existing
copy tests plus `FolderNavigationControllerTest`'s fixture needed the `checkMove` stub described
above. `FolderNavigationServiceTest` gained two `listChildrenOf` cases, including the one that
matters — it answers about another folder without moving where the service stands.
`FolderNavigationControllerTest` gained ten, the load-bearing ones being that `copyEntriesTo` reads
the *drop target* rather than `currentHandle()`, auto-renames against that folder's names, refuses
the whole drop with a `"copy"` toast and zero `copyNode` calls when the read fails, and does not
refetch this tab's listing when it isn't the destination. 335 tests pass; `appMegaExplorer` builds
`/W4`-clean.

### Deliberately not done

- **No Ctrl+drag for external file drops.** An OS drop is always a copy; the modifier means nothing
  there, and the upload branch of all six targets is untouched.
- **No Ctrl+Shift.** MEGA has no shortcut/symlink node, so Explorer's third combination has nothing
  to map onto.
- **Ctrl+drag from an unselected row still replaces the selection** (`selectRow(index, NoModifier)`),
  rather than adding to it the way a Ctrl+*click* would. Explorer drags just that item too.
- **The folder tree still isn't refreshed** after a drag-copy that creates a folder — Phase 16's,
  same as for 14a's moves.
- **No automated UI verification.** `ui-style`'s `drive` sends press→move→release as one step, so a
  modifier cannot be held across a drag; the gesture is verified by hand.

## Phase 20c — account section in the "More" menu (done)

Avatar / display name / email / storage bar / plan name, as a header above 20b's two entries and
Sign out. Unplanned and absent from the roadmap when it started; filed as 20c because it lands in
the menu Phase 20b built.

### Fetch policy: nothing at login, everything on first open

Three options were on the table — fetch at login, fetch once at first open, fetch on every open —
and the answer is a split. **Login gains nothing**: a user who never opens this menu costs zero
requests, which matters because `fetchNodes` is already the slow part of signing in (Phase 18).
"Once, at first open" was then adopted for all of it and *reverted for storage*: this app is
expected to stay signed in for days, so a one-shot snapshot goes stale on screen. Storage is
therefore re-read on every open, stale-while-revalidate — the previous numbers stay up while the new
ones are in flight, so the loading state is only ever visible on the very first open. Avatar and
display name keep the once-per-session rule.

`used` and `max` come from **one** source, `getSpecificAccountDetails`. `MegaApi::getCloudStorageUsed()`
is a cheaper local read and was rejected: mixing sources means the numerator and the denominator can
disagree about versions and rubbish-bin contents, and re-reading on every open already makes the
freshness argument moot.

### SDK layer

Four methods on `IMegaClient`, appended at the end of the interface rather than grouped next to
`currentUserHandle()` — the synchronous-exception tally in that header is *positional*
("`checkMove` is the third", "`hasSubfolders` the sixth"), so inserting higher up would have
renumbered four existing doc comments. `currentAccountIdentity` is the seventh such exception, for
the usual reason: email, user handle and avatar colour are state the SDK already holds.

Three pitfalls, none of which announce themselves:

- **`getUserAvatarColor()` wants the Base64 user handle**, not the `uint64_t` that
  `currentUserHandle()` returns. Passing a stringified number returns a *plausible* colour, so this
  is invisible in review and on screen. `MegaSdkClient` uses `MegaApi::getMyUserHandle()` (the
  `char*` one) for this and only this.
- **`getUserAvatar` must be given a file path, not a directory.** With a trailing separator the SDK
  synthesizes `<email>0.jpg` inside it. Same caller-resolves-the-path rule as `getThumbnail`.
- **A missing avatar is the normal case.** Measured against a real avatar-less account the code is
  `kENoEnt` (-9), `"Not found"` — but `megaapi.h` documents no code at all here, so the adapter
  treats *any* failure as "no avatar" and `AccountService` converts it into
  `AvatarOutcome::hasAvatar` rather than leaving that judgement to callers. No toast, ever.

`ThumbnailListener` was renamed `AttributeFileListener` and shared, since fetching an avatar to a
path is the same request shape as fetching a thumbnail; `TextResultListener` and
`AccountDetailsListener` are new.

`AccountService` is stateless like the other services. Its one non-obvious rule: first name and last
name are requested **sequentially, not in parallel**. Parallel would need a mutex around the join,
and `MockMegaClient` invokes its callbacks synchronously — so the second callback would run inside
the first one's lock and self-deadlock the test suite.

### `AccountController`: two signal groups, one generation counter

`profileChanged` and `storageChanged` rather than one `changed`, because the two halves have
different lifetimes (once per session vs. per open) and a single signal would re-evaluate the avatar
bindings on every menu open. A generation counter bumped by `refresh`/`reset`/`retryAccountInfo`
drops callbacks belonging to an account that has since been signed out, and an in-flight flag keeps
a fast reopen (or a double-clicked retry link) from issuing a second storage read. `reset()` is
called from `Main.qml`'s existing auth `Connections`, on the **LoggedOut** branch only.

Deliberately no `NotificationController`: the only user-visible failure is the storage read, which
has an inline retry link in the menu itself, and the avatar/name failures are ordinary.

### The trap: `Menu` animates its own height, and the content settles late

This is the part worth remembering. FluentWinUI3's `Menu` opens like this:

```qml
property real __heightScale: 1
height: __heightScale * implicitHeight
enter: Transition {
    NumberAnimation { property: "__heightScale"; from: 0.33; to: 1; duration: 250 }
}
```

— and its `contentItem` is a `ListView` with `clip: true`. So for 250ms the menu is a window showing
only the top slice of its content, and `implicitHeight` is that ListView's `contentHeight`, which
**extrapolates the size of rows it has not realized yet from the average of the ones it has**. A
150px header among 30px rows wrecks that average: the estimate moves the menu's height, the new
height realizes a different set of rows, the average changes, and the height moves again. Measured,
pre-fix: `279 → 218 → 238 → 279 → 250 → 271 → 277 → 278`. On screen that reads as the menu opening,
snapping shut and reopening — captured frame-by-frame as "email visible → email gone and the menu
shorter → email back".

Two rounds of fixes were needed, and the first was not enough:

1. **Make the content's height final from creation.** The display name arrives asynchronously, so
   its `Label` reserves its line unconditionally instead of being `visible`-toggled; the storage
   failure state puts its retry link on the same line as the numbers rather than swapping in a block
   of a different height; and the header stopped depending on `anchors.fill: parent` while the
   parent's `implicitHeight` depended on the layout (a round trip that needs a settle frame). This
   made the logged height monotonic — and the flicker was still visible, because it was never only
   about the content.
2. **Drop the height animation.** `enter` is overridden with a 120ms opacity fade. The menu now takes
   its final size in one step, which removes the ListView estimation loop *and* the per-frame resize
   of the native popup window (`popupType: Popup.Window`, below) at the same time. Verified
   frame-by-frame at 30ms intervals: the menu is full-size in the first captured frame, and only the
   storage numbers fill in afterwards, into their reserved line.

That was still not the end of it. The fade was reported as flickering on roughly one open in two, so
the menu was instrumented (`onOpened` / `onClosed` / `onVisibleChanged` / `onHeightChanged` /
`onXChanged` / `onYChanged` / `onOpacityChanged`, `console.log` into the app's own log file, which
carries millisecond timestamps) and driven through a dozen open/close cycles. The result rules the
QML layer out entirely: **the popup never closes**. `visible=false` and `closed` appear only where
Escape was pressed, and `height` never changes once open. What the log does show is this, on every
single open:

```
16.355 visible= true
16.356 opacity= 0.000     <- the transition's `from` value
16.427 opacity= 0.349     <- the next animated value, 71ms later (26-71ms across opens)
```

The popup window is mapped by the OS, and the render thread can present a frame of the
*un-animated* state before the animation's first tick lands — one to three frames in which the menu
shows itself, snaps to `from`, and only then animates in. That is the whole bug, and it is a
property of having an enter transition on a popup that owns a window, not of which property the
transition drives. Under the style's own `__heightScale` transition it reads as "appears at full
height, collapses, grows back", which is exactly what the original frame-by-frame capture showed;
under a fade it reads as "appears, vanishes, fades back". So `enter` is `null` and the menu simply
appears.

The general lesson is two-layered: a `Menu` whose entries are all one row tall can be animated by
height and one with a header cannot, because `ListView` height estimation and a growing clip
rectangle form a feedback loop — but on top of that, a popup that is its own window should not have
an enter transition at all.

Four smaller `Menu` findings from the same work:

- **`width: 280` is explicit on the menu.** A `Menu`'s contentItem is a `ListView`, which does not
  aggregate its children's `implicitWidth`, so the header's own 280 is ignored and the menu stays at
  the ~200 the three text entries need. The flip side is the risk that did *not* materialize: the
  header cannot widen the menu by accident either.
- **`popupType: Popup.Window`.** Since Qt 6.8 a `Menu` may resolve to `Popup.Native`, and the docs
  are explicit that the delegate is then not used for rendering — which would silently drop this
  header entirely. Windows is a native-menu platform, so this is insurance, not decoration.
- **`popup()` needed arguments.** Its default is "top-left corner at the cursor", and this button
  sits at the right edge of the window, so all 280px of the menu opened *outside* the window.
  `popup(width - moreMenu.width, height)` right-aligns it under the button.
- **The menu background turned out to be marbled**, reported once the header made it big enough to
  see. `dark|light/images/popup-background.png` is the only family of style assets whose interior is
  *not* a flat colour: it carries WinUI's acrylic grain, per-pixel noise a couple of levels either
  side of #353535. `Impl.StyleImage` draws it as a `BorderImage`, whose middle section is 102x90 in
  the asset and is **stretched** to fill the item — a ~2.7x bilinear magnification at this menu's
  size, which smears the grain into soft blobs. Setting the BorderImage's `horizontalTileMode` /
  `verticalTileMode` to `Repeat` puts the texture back at 1:1; corners, borders and the baked-in
  shadow are unaffected because a `BorderImage` never tiles its corners. It has to be reached
  imperatively from `Component.onCompleted` (the style owns the background item), by duck-typing on
  `horizontalTileMode` rather than by child index, since that item also holds the style's
  high-contrast rectangle. Ordinary menus are too small to magnify the grain enough to notice, and
  `Dialog` is unaffected entirely — its background is a plain `Rectangle`.

### QML: a plain `Item`, and a circle Qt won't draw for you

The header is a bare `Item` with `activeFocusOnTab: false`, never a `Control` or `AbstractButton`:
`Menu`'s Up/Down walk skips whatever is not tab-focusable, which is exactly how the `MenuSeparator`
below it is already skipped.

`Rectangle { radius: width / 2; clip: true }` does **not** round an `Image` — Qt Quick's `clip` is a
rectangular scissor and ignores `radius`. The avatar is masked with `MultiEffect` plus a
`layer.enabled` mask rectangle. The coloured-initial fallback is used both when there is no avatar
file and when one arrived but failed to decode, so a truncated JPEG leaves a letter rather than a
hole. `Image.cache: false` matters here: re-logging into the same account rewrites the same path,
and the cached pixmap would otherwise win.

FluentWinUI3's `ProgressBar` hides its fill at `value: 0` (`visible: … && control.value`, a
truthiness test), so it would have given the "track only, no fill" loading state for free — but the
bar is two `Rectangle`s anyway, because the fill also has to switch colour past 90%.

### Units

`storageText()` formats with `QLocale::DataSizeTraditionalFormat` (1024-based, labelled "GB").
Confirmed against a real account: the SDK reports a maximum of 16106127360 bytes and MEGA itself
calls that "15 GB", which is this format's answer and not SI's 16.1 GB. Both halves share one base
so the two numbers are always comparable.

### Testing

`AccountServiceTest` (13) and `AccountControllerTest` (16) are new; 364 tests pass and
`appMegaExplorer` builds `/W4`-clean.

### Deliberately not done / not verified

- **No transfer quota and no subscription or payment detail.** Out of the file-manager frame; the
  `getSpecificAccountDetails` call asks for storage and pro level only, which is also what
  `megaapi.h` asks callers to do.
- **`storageMax == 0`** (Business / Pro Flexi reporting an unlimited quota) is guarded — the ratio
  returns 0.0 and the text shows used alone — but has not been seen on a real account.
- **Keyboard navigation is unverified.** `ui-style`'s `drive` injects keys into the foreground
  window, and a popup window is not it, so Down never reaches an open menu. This reproduces
  identically on the pre-existing right-click menus, so it is a limitation of the tooling rather
  than anything this phase introduced.
- **No account switching**, and nothing here is editable from the app: the section is read-only.

### Known issues (open)

- **The first menu of a session is drawn 32px too high for a frame.** The `Menu`'s `x`/`y` are not
  evaluated until roughly 72ms *after* `onOpened` on the very first open — measured as
  `opened h=279 x=0 y=0` while `moreButton` was already 32x32, then `x=-248 y=32` — so the menu
  appears overlapping the toolbar and then drops into place. Every later open keeps the settled
  value and is correct from the first frame. Passing the position to `popup(x, y)` instead behaves
  identically, so this is popup-window creation ordering rather than the way the position is
  expressed; the binding form is kept only because it reads better. **Untried next step:**
  `popupType: Popup.Item`, which removes the separate native window and with it the
  show-before-position ordering. It would satisfy the `Popup.Native` concern that motivated
  `Popup.Window` just as well, at the cost of clipping the menu to the main window — currently
  harmless, since the menu is 280x279 and right-aligned inside it, but not at every window size.
- **An open→blink→open was reported at about one open in two and could not be reproduced after
  `enter: null`.** Six scripted open/close cycles and three frame-by-frame captures showed the menu
  fully drawn in the first captured frame and pixel-identical to the settled reference, and the
  instrumented log shows the popup never closing. The report may have been contaminated by mouse
  movement during the test, so it is recorded rather than closed. If it returns, re-instrument with
  the handlers listed above before changing anything — the log file is
  `$LOCALAPPDATA/MegaExplorer/MegaExplorer/MegaExplorer.log` (`MegaExplorer` nests twice) — and note
  that `ui-style`'s default `--method print` cannot capture a `Popup.Window` menu at all;
  `--method screen` is required, as a command flag rather than a step.
