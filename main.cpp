#include "app/Logging.h"
#include "core/AccountService.h"
#include "core/AuthService.h"
#include "core/DownloadService.h"
#include "core/FileOperationService.h"
#include "core/FolderNavigationService.h"
#include "core/FolderTreeService.h"
#include "core/PreviewService.h"
#include "core/QuickAccessService.h"
#include "core/SearchService.h"
#include "core/ThumbnailService.h"
#include "core/UploadService.h"
#include "mega/MegaSdkClient.h"
#include "platform/QSettingsPinnedFolderStore.h"
#include "platform/WindowsSessionStore.h"
#include "qml/AccountController.h"
#include "qml/AuthController.h"
#include "qml/BusyState.h"
#include "qml/ClipboardController.h"
#include "qml/DownloadController.h"
#include "qml/FileMutationController.h"
#include "qml/FolderNavigationController.h"
#include "qml/FolderTreeModel.h"
#include "qml/GuiThread.h"
#include "qml/NotificationController.h"
#include "qml/PreviewController.h"
#include "qml/PreviewImageProvider.h"
#include "qml/PreviewImageStore.h"
#include "qml/QuickAccessModel.h"
#include "qml/TabsController.h"
#include "qml/ThumbnailController.h"
#include "qml/UploadController.h"

#include <QDebug>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QStyleHints>

#include <memory>
#include <QtQml/qqmlextensionplugin.h>

// The QML module's type registration sits in a generated translation unit nothing
// references by name, so without this forcing reference the linker drops it and
// every QML_ELEMENT type goes unregistered. qt_import_qml_plugins() is no
// alternative -- it is documented as a no-op against a non-static Qt.
Q_IMPORT_QML_PLUGIN(MegaExplorerPlugin)

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    // QSettings resolves its registry location from these; without them it fails to
    // initialize and every read/write silently no-ops.
    QCoreApplication::setOrganizationName("MegaExplorer");
    QCoreApplication::setApplicationName("MegaExplorer");
    // Reaches the About dialog as QML's Qt.application.version.
    QCoreApplication::setApplicationVersion(QStringLiteral(MEGAEXPLORER_VERSION));

    // Must run before any other logging call: this is a WIN32_EXECUTABLE, so log
    // output reaches no visible destination at all until the file sink exists.
    installLogging();

    // Style-tuning aid: check a theme without flipping the Windows setting. Both
    // FluentWinUI3 and Theme.qml read styleHints.colorScheme, so one call covers the
    // whole UI. Unset keeps the default of following the OS.
    const QByteArray colorScheme = qgetenv("MEGAEXPLORER_COLOR_SCHEME").toLower();
    if (colorScheme == "light")
    {
        app.styleHints()->setColorScheme(Qt::ColorScheme::Light);
    }
    else if (colorScheme == "dark")
    {
        app.styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    }

    // AppLocalDataLocation, not AppDataLocation: same non-roaming rationale
    // as Logging.cpp's log file (see installLogging()).
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(cacheDir);

    // basePath decides where the SDK's state-cache DB lives, which is what makes a
    // session restore take 0.6s instead of re-fetching the whole node tree (measured
    // 385s on a 640k-node account). toNativeSeparators because the SDK appends its
    // own components with backslashes, and a mixed-separator path breaks as soon as
    // anything prefixes it with \\?\ for long-path support.
    auto client = std::make_shared<MegaSdkClient>(QDir::toNativeSeparators(cacheDir).toStdString());
    auto sessionStore =
        std::make_shared<WindowsSessionStore>((cacheDir + "/session.dat").toStdString());

    auto downloadService = std::make_shared<DownloadService>(client);
    auto uploadService = std::make_shared<UploadService>(client);
    // Shared across every tab: handle-keyed cache, no per-tab state. What is
    // inherently per-tab lives in tabFactory below instead.
    auto thumbnailService = std::make_shared<ThumbnailService>(client);
    // Shared too, but for the opposite reason: one preview shows at a time for the
    // whole window, so there is nothing per-tab to keep.
    auto previewService = std::make_shared<PreviewService>(client);
    auto previewImageStore = std::make_shared<PreviewImageStore>();
    // Stateless validate-and-pass-through, so one instance serves every tab.
    auto fileOperationService = std::make_shared<FileOperationService>(client);
    auto authService = std::make_shared<AuthService>(client, sessionStore);
    auto accountService = std::make_shared<AccountService>(client);
    // Declared before the controllers that hold non-owning pointers to it, since
    // stack locals are destroyed in reverse order. That covers the stack-allocated
    // holders only: a per-tab controller kept alive past tab close by an in-flight
    // callback outlives both of these, so neither may be touched from a destructor.
    NotificationController notifications;
    // Same reason as notifications for being declared here.
    ClipboardController clipboard;
    DownloadController downloadController(downloadService, &notifications);
    UploadController uploadController(uploadService, &notifications);
    AuthController authController(authService);
    AccountController accountController(accountService);
    PreviewController previewController(previewService, previewImageStore);

    // Shared: the side panel is chrome beside the tab content, not per-tab state.
    auto folderTreeService = std::make_shared<FolderTreeService>(client);
    FolderTreeModel folderTreeModel(folderTreeService);

    // Same shared side-panel scope. The store needs no resolved path, unlike
    // sessionStore: QSettings resolves its own location from the names set above.
    auto pinnedFolderStore = std::make_shared<QSettingsPinnedFolderStore>();
    auto quickAccessService = std::make_shared<QuickAccessService>(client, pinnedFolderStore);
    QuickAccessModel quickAccessModel(quickAccessService, &notifications);

    // Wires one tab's worth of navigation/mutation/thumbnail state, capturing the
    // app-lifetime pieces above. TabsController calls this whenever a new tab is
    // needed; it has no wiring knowledge of its own.
    auto tabFactory = [client, thumbnailService, fileOperationService, &notifications, &clipboard]()
        -> TabContext {
        auto navigationService = std::make_shared<FolderNavigationService>(client);
        auto searchService = std::make_shared<SearchService>(client, navigationService);
        // makeGuiOwned, not make_shared: these are QObjects, and an in-flight
        // callback can drop the last reference from the SDK thread once the tab is
        // closed (GuiThread.h explains the rest).
        //
        // Created here rather than captured: one spinner counter per tab, or every
        // tab would look busy whenever any tab is.
        auto busy = makeGuiOwned<BusyState>();
        auto navigation = makeGuiOwned<FolderNavigationController>(
            navigationService, searchService, busy, &notifications);
        auto mutations = makeGuiOwned<FileMutationController>(
            navigation, navigationService, fileOperationService, busy, &notifications, &clipboard);
        auto thumbnails = makeGuiOwned<ThumbnailController>(
            thumbnailService, navigation->fileListModelForThumbnails(), &notifications);
        return TabContext{std::move(navigationService),
                          std::move(searchService),
                          std::move(navigation),
                          std::move(mutations),
                          std::move(thumbnails)};
    };
    TabsController tabs(tabFactory, &uploadController);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("tabsController", &tabs);
    engine.rootContext()->setContextProperty("downloadController", &downloadController);
    engine.rootContext()->setContextProperty("uploadController", &uploadController);
    engine.rootContext()->setContextProperty("notificationController", &notifications);
    engine.rootContext()->setContextProperty("authController", &authController);
    engine.rootContext()->setContextProperty("folderTreeModel", &folderTreeModel);
    engine.rootContext()->setContextProperty("quickAccessModel", &quickAccessModel);
    engine.rootContext()->setContextProperty("clipboardController", &clipboard);
    engine.rootContext()->setContextProperty("accountController", &accountController);
    engine.rootContext()->setContextProperty("previewController", &previewController);
    // The engine takes ownership of the provider, which is why the bytes it serves
    // live in previewImageStore rather than in the provider itself.
    engine.addImageProvider(QStringLiteral("megapreview"),
                            new PreviewImageProvider(previewImageStore));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [] {
            QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);
    engine.loadFromModule("MegaExplorer", "Main");

    authController.restoreSession();

    const int exitCode = app.exec();

    // Explicit stop point, not left to ~MegaSdkClient: destroying MegaApi makes the
    // SDK thread fail every pending request before it joins, and client is destroyed
    // last, so those callbacks would land on services and controllers already gone.
    // Declaration order can't fix it -- a dozen shared_ptrs keep client alive.
    client->shutdown();
    return exitCode;
}
