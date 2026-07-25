# Architecture

File layout and the ports-and-adapters/dependency-injection design. `CLAUDE.md` has a condensed
directory overview; read this file when adding new core logic, a new adapter, or anything that
needs to decide "which layer does this belong in."

## Directory overview

- `main.cpp` — `QGuiApplication`/`QQmlApplicationEngine` bootstrap; loads the `MegaExplorer` QML
  module's `Main` component via `loadFromModule`. Also the **composition root** (see below).
- `Main.qml` — root `ApplicationWindow`. QML files are registered in `CMakeLists.txt`'s
  `qt_add_qml_module(... QML_FILES ...)` — new `.qml` files must be added there.
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
src/core/      IMegaClient.h, FileEntry.h, Result.h, FileListingService.{h,cpp}, and other
               SDK-free domain services (FolderNavigationService, SearchService,
               DownloadService, ThumbnailService, ...)
src/mega/      MegaSdkClient adapter — the only place allowed to include megaapi.h
src/platform/  RealFileSystem adapter — not yet created (needed once local caching starts,
               roadmap step 6)
src/qml/       C++ types exposed to QML (Q_PROPERTY etc.): FileListModel, controllers
tests/         GoogleTest-based unit tests, one per src/core service
```

## Design: testability and dependency injection

Decided 2026-07-24, before Phase 1. C++ has no reflection-based DI container in mainstream use —
use manual constructor injection against abstract interfaces, wired at a **composition root**. Do
not add a DI framework (Boost.DI, Fruit, etc.) — unneeded complexity at this project's size.

- **Ports**: `IMegaClient` (wraps `mega::MegaApi`, `src/core/IMegaClient.h`) — `login`,
  `fetchNodes`, `getRootChildren`/`getChildren` (return `std::vector<FileEntry>`,
  `src/core/FileEntry.h`), `search`, `download`, `getThumbnail`. `IFileSystem` (wraps
  `std::filesystem`) — not yet added, needed once local caching starts (roadmap step 6).
  Domain/UI code depends only on these, never on SDK or filesystem types directly.
- **Adapters**: `MegaSdkClient` (`src/mega/MegaSdkClient.h`/`.cpp`) implements `IMegaClient` over
  `mega::MegaApi` using a per-call, self-deleting `MegaRequestListener` subclass (`new
  Listener(...)` passed to `login()`/`fetchNodes()`/etc., `delete this` at the end of
  `onRequestFinish`) — same idiom reused for `DownloadListener`/`ThumbnailListener`.
  `RealFileSystem` — not yet added. `MegaSdkClient.cpp`/`.h` are the only files allowed to include
  `megaapi.h` or call `std::filesystem` directly.
- **`Result<T>`** (`src/core/Result.h`): hand-rolled success/failure wrapper (no `std::expected` —
  `CMAKE_CXX_STANDARD` isn't pinned in this project, so C++23 can't be assumed); has a
  `Result<void>` specialization for callbacks with no payload (e.g. `login`).
- **Composition root**: `main.cpp` `make_shared`s the concrete adapters and injects them via
  constructor (`std::shared_ptr<IPort>`). No service locator, no container.
- **Domain logic**: `src/core` services (`FileListingService`, `FolderNavigationService`,
  `SearchService`, `DownloadService`, `ThumbnailService`) depend only on `IMegaClient`, are
  SDK-free, and are unit-tested with a mocked `IMegaClient`. E.g. `FileListingService` chains
  login→fetchNodes→getRootChildren, short-circuiting to a failed `Result<...>` if any stage fails.
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
  would self-deadlock.
- **Testing**: GoogleTest/GoogleMock, pulled via vcpkg's `sdk-tests` feature (see `docs/BUILD.md`).
  `MockMegaClient` (`tests/FileListingServiceTest.cpp`) `MOCK_METHOD`-fakes `IMegaClient` and
  drives its callback args with `testing::InvokeArgument`. `IFileSystem` has no fake yet (not
  added).
- **`MegaExplorerCore`**: a static library target (root `CMakeLists.txt`) bundling `src/core`'s
  SDK-free headers/`.cpp` files. Exists so `appMegaExplorer` and `MegaExplorerTests` link the same
  compiled domain logic instead of each recompiling it standalone.
