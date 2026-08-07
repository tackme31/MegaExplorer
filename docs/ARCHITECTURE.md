# Architecture

File layout and the ports-and-adapters/dependency-injection design. `CLAUDE.md` has a condensed
directory overview; read this file when adding new core logic, a new adapter, or anything that
needs to decide "which layer does this belong in."

## Directory overview

- `main.cpp` — `QGuiApplication`/`QQmlApplicationEngine` bootstrap; loads the `MegaExplorer` QML
  module's `Main` component via `loadFromModule` (by type name, not path — unaffected by where the
  `.qml` file physically lives). Also the **composition root** (see below).
- `qml/` — all hand-written `.qml` files (as opposed to `src/qml/`, which is the C++ types exposed
  *to* QML — see below). The module's backing target is the `MegaExplorerQml` library (see the
  target list below), and the repo root directory is itself named `MegaExplorer`, which is what
  Qt's `qt_add_qml_module` checks the URI against; subdirectories under `qml/` are free-form and,
  since Qt 6.8 (`QTP0004`, on by default at this project's required Qt 6.10), each subdirectory
  gets its own auto-generated `qmldir` that prefers the module's root — so types in different
  subdirectories resolve each other with no explicit imports. Organized by role, split further
  only once a subfolder actually earns it:
  - `qml/Main.qml` — root `ApplicationWindow`.
  - `qml/views/` — full-screen views, e.g. `LoginView.qml`, `FileTableView.qml`.
  - `qml/components/` — everything reusable, including the modal dialogs (`AboutDialog.qml`,
    `NewFolderDialog.qml`, …). A separate `qml/dialogs/` was planned but never earned its keep.
  New `.qml` files must also be added to `CMakeLists.txt`'s `qt_add_qml_module(... QML_FILES ...)`.
- `importedcontent/` — Figma-to-Qt export drop-in; auto-`add_subdirectory`'d if its
  `CMakeLists.txt` exists. Currently empty.
- `third_party/sdk` — `meganz/sdk` submodule, exposes `MEGA::SDKlib`. Vendored, do not edit.
- `third_party/vcpkg` — builds the SDK's third-party deps per `third_party/sdk/vcpkg.json`.
- `build/` — out-of-source, regeneratable. Multiple dirs exist: stale MinGW one, the current MSVC
  one, `msvc-debug` (via `CMakePresets.json`'s `msvc-debug` preset, used by Qt Creator), and
  `sdk-msvc-debug` (standalone SDK validation, see `docs/BUILD.md`).
- `CMakePresets.json` — the `msvc-debug` preset; see `docs/BUILD.md` for why it exists.
- `.clangd` — stale, points at a MinGW compile-commands path that doesn't match any real build dir
  (typo + predates the SDK/MSVC switch). Not fixed for MSVC either: the VS-generator build can't
  emit `compile_commands.json`. clangd/IntelliSense currently lags for MSVC+SDK code.

`src/core`/`src/mega` wrap the SDK's C++ API (`megaapi.h`'s
`MegaApi`/`MegaRequestListener`/`MegaNode` — see
`third_party/sdk/examples/simple_client/simple_client.cpp` for the login/fetchNodes pattern this
was validated against). Any further MEGA-facing code must go through `IMegaClient`/`FileEntry`,
never call `MegaApi`/`std::filesystem` directly.

```
src/app/       Cross-cutting Qt-dependent app infrastructure, not QML-facing: Logging.{h,cpp}
               (categorized QLoggingCategory + qInstallMessageHandler file sink)
src/core/      IMegaClient.h, ISessionStore.h, FileEntry.h, Result.h,
               AuthService.{h,cpp}, and other SDK-free domain services
               (FolderNavigationService, SearchService, DownloadService, ThumbnailService, ...)
src/mega/      MegaSdkClient adapter and MegaSdkLogger (bridges mega::MegaLogger into
               src/app/Logging.h's lcSdk category) — the only files allowed to include
               megaapi.h
src/platform/  Local-storage adapters, not part of MegaExplorerCore (parallels src/mega):
               WindowsSessionStore (session token persistence via Windows DPAPI) and
               QSettingsPinnedFolderStore (quick-access pin list as JSON under QSettings'
               quickAccess/accounts/<accountKey>/pinnedFolders key, one per MEGA account
               since Phase 11a -- see docs/PROGRESS.md's Phase 11a log). Both get their own
               adapter-level test (tests/WindowsSessionStoreTest.cpp,
               tests/QSettingsPinnedFolderStoreTest.cpp) since -- unlike MegaSdkClient --
               neither needs a live account.
src/qml/       C++ types exposed to QML (Q_PROPERTY etc.): FileListModel, controllers,
               NotificationController (shared error-toast relay)
tests/         GoogleTest-based unit tests, one per src/core service
```

### CMake targets

Five targets; all but the last are defined in the root `CMakeLists.txt`:

| Target | Holds | Why it is separate |
| --- | --- | --- |
| `MegaExplorerCore` (STATIC) | `src/core` | So `appMegaExplorer` and `MegaExplorerTests` link the same domain logic. Links no Qt at all. |
| `MegaExplorerQml` (STATIC) | `src/qml` + `qml/`, and carries `qt_add_qml_module` | Same reason, one level up: the QML module's backing target is a library rather than the executable so `MegaExplorerTests` can link the QML-facing C++ types instead of hand-copying the file list, and so a Qt Quick Test target can `import MegaExplorer` at all. |
| `appMegaExplorer` (WIN32 executable) | `main.cpp` + `src/app`, `src/mega`, `src/platform` | The composition root and the only target that touches the SDK or the OS. |
| `MegaExplorerTests` | `tests/` | GoogleTest binary; links the two libraries above. |
| `MegaExplorerQmlTests` (`tests/qml/CMakeLists.txt`) | `tests/qml/` | Qt Quick Test binary, separate from `MegaExplorerTests` because it needs a `QGuiApplication` and a QML engine, which `quick_test_main()` supplies and `tests/TestMain.cpp`'s `QCoreApplication` cannot. |

Two consequences worth knowing before editing the build files:

- Because the backing target is a **library**, `qt_add_qml_module` no longer appends the module's
  target path to the output directory by itself — `OUTPUT_DIRECTORY` is pinned explicitly so
  `qmldir` keeps landing in `build/<preset>/MegaExplorer/`.
- A static backing library means the generated type registration lives in a translation unit that
  nothing references by name, so the linker would drop it and every QML type would fail to
  register at startup. `appMegaExplorer` therefore links `MegaExplorerQmlplugin` *and* `main.cpp`
  carries `Q_IMPORT_QML_PLUGIN(MegaExplorerPlugin)`. Neither half works without the other, and
  `MegaExplorerQmlTests` repeats both in `tests/qml/QmlTestMain.cpp` for the same reason.

Note the split: `src/qml/` is C++ (`.h`/`.cpp`) that QML consumes; `qml/` (project root) is the
`.qml` files themselves. Don't conflate the two when adding a feature that needs both a controller
and a view/component.

## Design: testability and dependency injection

Decided 2026-07-24, before Phase 1. C++ has no reflection-based DI container in mainstream use —
use manual constructor injection against abstract interfaces, wired at a **composition root**. Do
not add a DI framework (Boost.DI, Fruit, etc.) — unneeded complexity at this project's size.

- **Ports**: `IMegaClient` (wraps `mega::MegaApi`, `src/core/IMegaClient.h`) — `login`,
  `fetchNodes`, `getRootChildren`/`getChildren` (return `std::vector<FileEntry>`,
  `src/core/FileEntry.h`), `search`, `download`, `getThumbnail`. Domain/UI code depends only on
  this port, never on SDK types directly. There's no local node-tree cache port/adapter anymore
  (`INodeCache`/`SqliteNodeCache`, added Phase 6, removed Phase 7b — see `docs/PROGRESS.md`):
  folder listings are always fetched live from the network.
- **Adapters**: `MegaSdkClient` (`src/mega/MegaSdkClient.h`/`.cpp`) implements `IMegaClient` over
  `mega::MegaApi` using a per-call, self-deleting `MegaRequestListener` subclass (`new
  Listener(...)` passed to `login()`/`fetchNodes()`/etc., `delete this` at the end of
  `onRequestFinish`) — same idiom reused for `DownloadListener`/`ThumbnailListener`.
  `MegaSdkClient.cpp`/`.h` and `MegaSdkLogger.cpp`/`.h` (a small `mega::MegaLogger` bridge into
  `src/app/Logging.h`'s categorized logging, registered/unregistered in `MegaSdkClient`'s
  constructor/destructor via `MegaApi::addLoggerObject`) are the only files allowed to include
  `megaapi.h`.
- **`Result<T>`** (`src/core/Result.h`): hand-rolled success/failure wrapper (no `std::expected` —
  `CMAKE_CXX_STANDARD` isn't pinned in this project, so C++23 can't be assumed); has a
  `Result<void>` specialization for callbacks with no payload (e.g. `login`).
- **Composition root**: `main.cpp` `make_shared`s the concrete adapters and injects them via
  constructor (`std::shared_ptr<IPort>`). No service locator, no container.
- **Domain logic**: `src/core` services (`AuthService`, `FolderNavigationService`, `SearchService`,
  `DownloadService`, `ThumbnailService`) depend only on `IMegaClient` (`AuthService` also on
  `ISessionStore`), are SDK-free, and are unit-tested with mocks. E.g. `AuthService` chains
  login/restoreSession→fetchNodes→best-effort session persistence, deliberately not depending on
  `FolderNavigationService` itself — resetting navigation state on logout/re-login is the QML
  layer's responsibility (`FolderNavigationController::reset`), not this service's.
- **Async seam**: `MegaApi` is listener/callback-based (see `MegaListener::onRequestFinish` in
  `simple_client.cpp`) — `IMegaClient` methods take a completion callback
  (`std::function<void(Result<...>)>`), not a synchronous return, so test fakes can simulate
  success/failure/timeout. `login`/`fetchNodes`/`download`/`getThumbnail` callbacks fire on an
  SDK-internal background thread; `getRootChildren`/`getChildren` are synchronous under the hood
  (`MegaApi::getRootNode()`/`getChildren()`, not request/listener-based) but kept callback-shaped
  for interface consistency. `search()` is also synchronous and deliberately called straight from
  the GUI thread (blocks it) rather than marshaled — see Phase 3 in `docs/PROGRESS.md` for why.
  Services whose callbacks can fire off the GUI thread (`DownloadService`, `ThumbnailService`) take
  their own `std::mutex`; `IMegaClient` calls themselves are made with no lock held, since
  `MockMegaClient`-based tests invoke callbacks synchronously from that same call and a held lock
  would self-deadlock. `FolderNavigationService::openRoot`/`openFolder`/`goBack`/`refreshCurrent`
  all share the same single-callback shape: `onDone` fires exactly once with the authoritative
  `IMegaClient` result (this used to be a two-callback cache-then-refresh shape in Phase 6; removed
  in Phase 7b — see `docs/PROGRESS.md`).
- **Testing**: GoogleTest/GoogleMock, pulled via vcpkg's `sdk-tests` feature (see `docs/BUILD.md`).
  `MockMegaClient` (`tests/MockMegaClient.h`) `MOCK_METHOD`-fakes `IMegaClient`; mocks drive
  callback args with `testing::InvokeArgument`. **What gets a test is decided by what a class does,
  not by which directory it sits in.** This is the only place that rule lives — don't restate it in
  per-file comments, which is how it previously rotted into nine mutually-citing copies:
  - `src/core` — all of it. SDK-free, Qt-free domain logic; nothing here has an excuse.
  - `src/qml` — everything that holds state or branches on it (the models and the controllers) is
    tested, driving a *real* `src/core` service over `MockMegaClient` rather than mocking the
    service. Exactly two kinds of file are exempt: one-line wrappers over a Qt/OS API
    (`KeyboardState`) and types that mean nothing without a live QML engine (`WindowAgentForeign`).
    Rendering and gestures live in `qml/`, not here, so "it's GUI glue" is not a reason to skip a
    class.
  - `src/mega` — none. `MegaSdkClient` would need a live MEGA account.
  - `src/platform` — all of it, driven as the real adapter (no mock stands in for DPAPI or
    `QSettings`). Each takes its storage location through the constructor, so a test points it at a
    temp file instead of the user's session file or registry — `QSettingsPinnedFolderStore`'s
    optional INI path exists for exactly that and is empty in production.
  - `qml/` — the functions whose result is decided by their arguments, and nothing else. Qt Quick
    Test in `tests/qml/` covers `ToastStack`'s message composition, `ActionCatalog`'s per-action
    label/icon/enabled/trigger, and `DragProxy.canDropOn`; all three fail *silently* when wrong,
    producing a wrong sentence or a drop target that lights up when it shouldn't. What is not
    tested is the views whose substance is rendering and gestures — `FileTableView`,
    `FileGridView`, `TabStrip` — where a Qt Quick Test asserts on layout accidents and breaks on
    every restyle. R4-5 in `docs/REFACTOR_PLANS.md` has the reasoning.

  Three gaps the rule says to close and nothing has yet — all gaps, not decisions:
  `MenuActions` (small: it maps a two-valued site enum onto `MenuActionResolver`, which has 47
  cases of its own); `ActionCatalog`'s five `trigger` lambdas that name the `downloadController`
  /`tabsController`/`quickAccessModel`/`clipboardController` context properties, since only
  `main.cpp` ever sets those; and `ToastStack.push`'s oldest-first trimming, whose `ListModel` is
  private to the component. R4 in `docs/REFACTOR_PLANS.md` records each assessment.
- **Known limit of the suite**: no test in this repo runs more than one thread. `MockMegaClient`
  delivers every completion synchronously on the calling thread via `testing::InvokeArgument`, so
  the `std::mutex`es in `DownloadService`/`UploadService`/`ThumbnailService` never contend and
  `makeGuiOwned`'s cross-thread branch (`src/qml/GuiThread.h`) is never reached — deleting either
  would leave the suite green. Everything the "Threading model" section below asserts is held up by
  review, not by a test. Windows has no ThreadSanitizer under MSVC or clang-cl either, so no build
  flag closes this; see R4-6 in `docs/REFACTOR_PLANS.md`.
- **`MegaExplorerCore`**: a static library target (root `CMakeLists.txt`) bundling `src/core`'s
  SDK-free headers/`.cpp` files. Exists so `appMegaExplorer` and `MegaExplorerTests` link the same
  compiled domain logic instead of each recompiling it standalone.

## Threading model

There are exactly **two** threads, and every rule below follows from that.

- The **GUI thread** runs Qt/QML and all of `src/qml`.
- **One SDK thread** delivers every callback. `MegaApiImpl` starts a single loop thread
  (`third_party/sdk/src/megaapi_impl.cpp:7140`), and completions raised on other SDK workers are
  marshaled onto it (`:17992-18011`, `executeOnThread`). Listeners therefore never run concurrently
  with each other — the only race this app can have is SDK thread vs GUI thread.

`src/core` services that can be called back off the GUI thread (`DownloadService`, `UploadService`,
`ThumbnailService`) carry their own `std::mutex`; those that only ever see `IMegaClient`'s
synchronous delivery modes (`FolderNavigationService`, `QuickAccessService`) carry none. `src/qml`
controllers hop back with `invokeOnGuiThread` (`src/qml/GuiThread.h`), and QObject-owning
controllers are destroyed on the GUI thread via `makeGuiOwned` because an in-flight callback can
drop the last `shared_ptr` from the SDK thread. The three delivery modes themselves (truly async /
always synchronous / synchronous on failure only) are documented at the top of `src/core/IMegaClient.h`.

Callbacks are delivered **while the SDK still holds its `sdkMutex`** (`megaapi_impl.cpp:20809`,
`:19904`, `:18046`). Two standing rules come out of that fact:

- **The synchronous `IMegaClient` methods can block the GUI thread — by design, not by accident.**
  All of them (`currentSessionToken`, `currentUserHandle`, `checkMove`, `checkUpload`,
  `findChildFiles`, `hasSubfolders`, `currentAccountIdentity`, plus `getChildren`/`search`/`getPath`)
  take `sdkMutex` inside the SDK, so the node tree can never be read torn — there is no data race
  here. What is real is the *wait*: `sdkMutex` is a `recursive_timed_mutex` (`megaapi_impl.h:5307`)
  that the SDK loop holds across `client->exec()` (`:8147`), which is where the multi-minute
  `fetchNodes` decrypt runs. A `checkMove` from a hovering drag or a `hasSubfolders` from the folder
  tree issued during that window freezes the window for its duration. It is a latency property of
  the "answer right now" interface shape, not something a lock can fix.
- **Never use `Qt::BlockingQueuedConnection` on a callback path.** A single one deadlocks the app
  permanently: the SDK thread would hold `sdkMutex` while waiting on the GUI thread, and the GUI
  thread is liable to be inside `checkMove` waiting for that same `sdkMutex`. Every connection in
  the codebase is `Qt::QueuedConnection` today, and must stay that way.

Two more SDK-contract details that are easy to get wrong later:

- Re-entering the SDK from inside a callback (`ThumbnailService::finishJob` → `startNextIfCapacity`
  → `getThumbnail` → `getNodeByHandle`) is safe **only because `sdkMutex` is recursive** — a
  dependency on an SDK implementation detail, not on documented API behavior.
- **Never call `MegaApi::removeRequestListener`.** The SDK deletes requests and transfers but never
  listeners (`:18032`, `:18192`), so `MegaSdkClient`'s listeners `delete this` in
  `onRequestFinish`/`onTransferFinish`; `removeRequestListener` does `setListener(NULL)`
  (`:17895-17903`), which suppresses the very callback that would free them and leaks every one.

## Trust boundary: strings that come from the server

MEGA node names (`FileEntry::name`) are **untrusted input**. Nothing upstream validates them: MEGA
is end-to-end encrypted, so the server cannot inspect a name even in principle, and a name can
arrive from outside this app entirely — a public-link import keeps the original name, and any other
client can rename a node to anything. `FileOperationService::isValidName` guards only names *this*
app creates (rename / new folder), so it is an entry-side rule, not a guarantee about what is
already in the account.

Consequently, every place a node name becomes part of a local path or an OS-level action needs its
own defense:

- **Downloads** — `DownloadController::computeDestinationPath` joins the Downloads directory with
  the node name, and `DownloadService::safeLocalFileName` (a static, Qt-free rule, unit-tested in
  `tests/DownloadServiceTest.cpp`) is what keeps the result a leaf inside that directory. It keeps
  only the part after the last separator, drops a drive-letter prefix, replaces control characters
  and `<>:"|?*`, strips trailing dots/spaces, sidesteps Windows' reserved device names, and falls
  back to `download` when nothing usable is left. Sanitizing rather than rejecting is deliberate:
  `CON.txt` is a legitimate MEGA name and refusing it would make a real file undownloadable. The
  MEGA SDK offers no help here — `MegaApi::startDownload` escapes only its `customName` argument,
  which `MegaSdkClient::download` passes as `nullptr`.
- **Local paths derived from other node metadata are safe by construction** — thumbnail and avatar
  cache files are named from the numeric handle (`src/qml/ThumbnailController.cpp`,
  `src/qml/AccountController.cpp`) and the log file name is fixed (`src/app/Logging.cpp`), so no
  server-controlled string reaches those.

The mirror image — **local** data crossing into the app — is `UploadController::dropUrls`, and it
is the model to copy: it drops anything that isn't `QUrl::isLocalFile()`, classifies with
`QFileInfo::isDir`/`isFile`, and normalizes via `absoluteFilePath()` before the path is used.

## Error representation

Failure is expressed three ways, and they are not interchangeable:

1. **`Result<T>`** (`src/core/Result.h`) — `success` / `value()` / `errorMessage` / `errorCode`. The
   normal one; every `IMegaClient` method and every service reports through it.
2. **A `Result` that always succeeds, with the error inside the value** — `AccountService::loadAvatar`
   returns `Result<AvatarOutcome>::ok()` unconditionally and puts the failure in
   `AvatarOutcome::errorCode`/`errorMessage`. The outer `Result` carries zero bits there. Don't add
   more of these; if the work can't fail, don't wrap it in a `Result` at all.
3. **Job state machines** — `DownloadJob` / `UploadJob` carry `state == Failed` plus their own copies
   of `errorMessage`/`errorCode`, fed from the `Result` that produced them.

### `errorCode` is the part callers are allowed to read

**Branch on `errorCode`, never on `errorMessage`.** On the SDK path the message is
`MegaError::getErrorString()` — a fixed English table looked up from the code itself
(`third_party/sdk/src/megaapi.cpp`), so it carries nothing the code doesn't. It is for logs and for
the one UI case that has genuinely run out of classification (`AuthController`'s `UnknownError`).
`IMegaClient.h` stated this rule for its three synchronous methods; it applies everywhere.

Codes come from `src/core/MegaErrorCodes.h`, whose value ranges keep app-defined codes from being
mistaken for SDK ones:

- **`0`** is `API_OK` and is never valid on a failed `Result`. Note that a default-constructed
  `Result` is exactly that contradiction (`success == false`, `errorCode == 0`) — always build one
  through `ok()` or `fail()`.
- **negative** mirrors `mega::MegaError`, kept honest by the `static_assert` block at the top of
  `src/mega/MegaSdkClient.cpp` — the only file that can see both that header and `megaapi.h`.
- **positive** is this app's own sentinels, so they fall through every `switch` written against SDK
  values. `MegaErrorCodes.h` holds the ledger of which numbers are taken.

`Result::fail` takes the code as a **required** argument. It used to default to `-1`, which is
`API_EINTERNAL`, so "the caller didn't think about it" and "an internal error happened" were the
same value; making it mandatory is what forces the decision at each call site.

### `value()` may only be read on a successful `Result`

The value member is private and reached through `value()`, which asserts `success`. Reading it on a
failed `Result` hands back a default-constructed `T` — an empty string, `0`, an empty vector — which
is indistinguishable from real data at the call site; the assert turns that silent misread into a
Debug crash on the offending line. Release builds compile the assert out and return the
default-constructed value, so this is a development guardrail, not a runtime check.

The one place that reads `value()` without a local `success` check is `QuickAccessModel`'s
`PinStatus::Usable` branch, which is safe because `QuickAccessService::classify` only returns
`Usable` from inside its own `success` branch. Every other read is inside a success branch or behind
an early return.

`std::expected` (C++23) would enforce this in the type system instead, but the project is C++17 and
raising the standard would drag `third_party/sdk` and `qwindowkit` along with it. Rejected on cost
in `docs/REFACTOR_PLANS.md` R3-9; the hand-rolled `Result` plus this assert is the settled answer.

### A `Result` may not be dropped on the floor

`Result` is `[[nodiscard]]`, so ignoring one is a compile warning (MSVC C4834). Ignoring a failure
on purpose is spelled `(void)foo();` with a comment saying why — `AuthService`'s best-effort session
clears are the model. This was added after `QuickAccessService` discarded four `IPinnedFolderStore::save()`
results outright, which meant a pin the user added just silently wasn't there next launch.

Where a Qt-free service has no one to report to, it hands the failure out rather than swallowing it:
`QuickAccessService::load()` returns its `Result` for the caller to log, and its write-through
failures go to a `setOnPersistenceFailed` handler that `QuickAccessModel` turns into a log line plus
a toast. Services don't log; `src/qml` does.

### Collapsing a code to a verdict: unknown means "don't act"

Some boundaries have to reduce an `errorCode` to a decision. Two rules there:

- **Name the definitive codes; let everything else fall through to non-definitive.** A `switch` with
  an allowlist and a `default` is the shape — `isSessionDefinitivelyInvalid` (`AuthService.cpp`) and
  `QuickAccessService::classify` (`QuickAccessService.cpp`) both use it. A code added to the SDK
  later then arrives as "unknown", not as a licence to act.
- **The non-definitive branch takes the side that destroys nothing.** An unrecognized login failure
  keeps the stored session; an unanswerable pin lookup keeps the pin. The asymmetry is the whole
  argument: a wrongly-kept dead session costs one failed launch, a wrongly-cleared good one costs
  the user's password.

Don't reduce to `bool` when the boundary needs three answers. `QuickAccessService::classify` returns
`Usable`/`Gone`/`Unknown` because it used to be a `bool`, and folding "couldn't ask" into "it's
gone" meant a shutdown during the login-time sweep wrote an emptied pin list to disk.

### C++ carries the reason, QML carries the words

**No user-facing sentence is composed in C++, and no SDK string is composed into one.** A controller
reporting a failure sends two structured values and nothing else:

- **`context`** — which operation failed (`"navigation"`, `"refresh"`, `"paste"`, …). Selects the
  clause naming the operation.
- **A reason** — the `errorCode`, already collapsed by the "unknown means don't act" rule above.
  Selects the clause explaining why.

`NotificationController::notifyError` is where that happens for toasts: it classifies the code once
into `ErrorReason` (`NotFound`/`NoPermission`/`Offline`/`Unknown`) and emits `errorOccurred`, and
`ToastStack.qml`'s `showError`/`describeReason` join the two clauses. `AuthController` does the same
for the login screen with `AuthErrorKind` and `LoginView.qml`'s `describeError`. Call sites classify
nothing themselves — there is one table per boundary, not one per caller.

**The SDK's English reaches the user in exactly one case: an `Unknown` reason.** Both boundaries
carry the raw string alongside the reason and both blank it out once classification succeeded
(`NotificationController`'s `rawMessage` argument, `AuthController::rawErrorMessage`) — a classified
failure has a translated sentence, so appending untranslated text to it would only make it worse.
Everywhere else the string is for the log.

A failure this app rejected before the SDK saw it has no code to classify and no string worth
showing — an invalid name, a local settings write, a pre-flight check. Those use the
context-only `notifyError(context)` overload, and their QML case is a fixed sentence that reads
neither reason nor raw message.

The asymmetry to keep in mind: C++ can add a context QML doesn't handle, and no compiler will say
so. `showError`'s `default` branch therefore `console.warn`s rather than printing the raw string,
which is how the missing `"refresh"` case survived from Phase 20a to R3-5 — it looked like a
message rather than like a gap.
