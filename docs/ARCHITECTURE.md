# Architecture

File layout and the ports-and-adapters/dependency-injection design. `CLAUDE.md` has a condensed
directory overview; read this file when adding new core logic, a new adapter, or anything that
needs to decide "which layer does this belong in."

## Directory overview

- `main.cpp` — `QGuiApplication`/`QQmlApplicationEngine` bootstrap; loads the `MegaExplorer` QML
  module's `Main` component via `loadFromModule` (by type name, not path — unaffected by where the
  `.qml` file physically lives). Also the **composition root** (see below).
- `qml/` — all hand-written `.qml` files (as opposed to `src/qml/`, which is the C++ types exposed
  *to* QML — see below). `appMegaExplorer` is itself the QML module's backing target, so per Qt's
  `qt_add_qml_module` docs the on-disk layout doesn't need to mirror the module's URI path;
  subdirectories are free-form and, since Qt 6.8 (`QTP0004`, on by default at this project's
  required Qt 6.10), each subdirectory gets its own auto-generated `qmldir` that prefers the
  module's root — so types in different subdirectories resolve each other with no explicit
  imports. Organized by role, split further only once a subfolder actually earns it:
  - `qml/Main.qml` — root `ApplicationWindow`.
  - `qml/views/` — full-screen views (empty until Phase 6+ adds one).
  - `qml/components/` — reusable, non-modal pieces, e.g. `DownloadSnackbar.qml`.
  - `qml/dialogs/` — modal popups/dialogs (empty so far).
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
               since Phase 11a -- see docs/PROGRESS.md's Phase 11a log). WindowsSessionStore
               gets its own adapter-level test (tests/WindowsSessionStoreTest.cpp) since --
               unlike MegaSdkClient -- it needs no live account; QSettingsPinnedFolderStore
               doesn't, since QSettings writes to the real per-user registry (see
               docs/PROGRESS.md's Phase 11 log).
src/qml/       C++ types exposed to QML (Q_PROPERTY etc.): FileListModel, controllers,
               NotificationController (shared error-toast relay)
tests/         GoogleTest-based unit tests, one per src/core service
```

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
  callback args with `testing::InvokeArgument`. `MegaSdkClient` itself has no adapter-level test
  since it needs a live MEGA account.
- **`MegaExplorerCore`**: a static library target (root `CMakeLists.txt`) bundling `src/core`'s
  SDK-free headers/`.cpp` files. Exists so `appMegaExplorer` and `MegaExplorerTests` link the same
  compiled domain logic instead of each recompiling it standalone.

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
