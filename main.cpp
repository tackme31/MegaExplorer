#include "app/Logging.h"
#include "core/AccountService.h"
#include "core/AuthService.h"
#include "core/DownloadService.h"
#include "core/FileOperationService.h"
#include "core/FolderNavigationService.h"
#include "core/FolderTreeService.h"
#include "core/QuickAccessService.h"
#include "core/SearchService.h"
#include "core/ThumbnailService.h"
#include "core/UploadService.h"
#include "mega/MegaSdkClient.h"
#include "platform/QSettingsPinnedFolderStore.h"
#include "platform/WindowsSessionStore.h"
#include "qml/AccountController.h"
#include "qml/AuthController.h"
#include "qml/ClipboardController.h"
#include "qml/DownloadController.h"
#include "qml/FolderNavigationController.h"
#include "qml/FolderTreeModel.h"
#include "qml/GuiThread.h"
#include "qml/NotificationController.h"
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

// The MegaExplorer QML module is backed by a static library (MegaExplorerQml),
// whose type registration sits in a generated translation unit nothing here
// references by name -- so without this forcing reference the linker drops it
// and every QML_ELEMENT type goes unregistered. qt_import_qml_plugins() is not
// an alternative: it is documented as a no-op against a non-static Qt.
Q_IMPORT_QML_PLUGIN(MegaExplorerPlugin)

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    // QML Settings (view mode persistence) needs these to resolve a
    // per-app registry/config location; without them QSettings fails to
    // initialize (Status code 1) and every read/write silently no-ops.
    QCoreApplication::setOrganizationName("MegaExplorer");
    QCoreApplication::setApplicationName("MegaExplorer");
    // Reaches the About dialog as QML's Qt.application.version, so no separate
    // type has to exist just to carry one string.
    QCoreApplication::setApplicationVersion(QStringLiteral(MEGAEXPLORER_VERSION));

    // Must run before any other logging call: appMegaExplorer is
    // WIN32_EXECUTABLE, so qWarning()/qCWarning() output reaches no visible
    // destination at all on a normal launch until this installs a file sink.
    installLogging();

    // Style-tuning aid: check the light theme without flipping the Windows
    // setting. One call covers both halves of the UI -- FluentWinUI3 and
    // Theme.qml's isLight both read styleHints.colorScheme, which this
    // overrides application-wide. Unset (or any other value) keeps the
    // default behaviour of following the OS.
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

    // Computed before the client because MegaSdkClient's basePath decides
    // where the SDK's state-cache DB lives, and that DB is what makes a
    // session restore take 0.6s instead of re-fetching the whole node tree
    // (measured: 385s on a 640k-node account -- docs/PROGRESS.md Phase 18).
    // toNativeSeparators because the SDK appends its own path components with
    // backslashes, and a mixed-separator path breaks as soon as anything
    // prefixes it with \\?\ for long-path support.
    auto client = std::make_shared<MegaSdkClient>(QDir::toNativeSeparators(cacheDir).toStdString());
    auto sessionStore =
        std::make_shared<WindowsSessionStore>((cacheDir + "/session.dat").toStdString());

    auto downloadService = std::make_shared<DownloadService>(client);
    auto uploadService = std::make_shared<UploadService>(client);
    // Shared across every tab (handle-keyed cache, no per-tab state) --
    // unlike FolderNavigationService/SearchService/FolderNavigationController
    // below, which are inherently per-tab and so live in tabFactory instead.
    auto thumbnailService = std::make_shared<ThumbnailService>(client);
    // Stateless validate-and-pass-through, so one instance is shared by every
    // tab's FolderNavigationController (same rationale as thumbnailService).
    auto fileOperationService = std::make_shared<FileOperationService>(client);
    auto authService = std::make_shared<AuthService>(client, sessionStore);
    // Stateless like fileOperationService; one instance behind the account
    // section of the "More" menu.
    auto accountService = std::make_shared<AccountService>(client);
    // Declared before the controllers below: they hold a non-owning pointer
    // to it, and stack locals are destroyed in reverse construction order.
    // That covers the stack-allocated holders only -- a per-tab controller kept
    // alive past tab close by an in-flight callback (see makeGuiOwned in
    // tabFactory) can outlive both of these, so neither may be touched from a
    // controller's destructor.
    NotificationController notifications;
    // Same reason as notifications above for being declared here: every tab's
    // FolderNavigationController holds a non-owning pointer to it.
    ClipboardController clipboard;
    DownloadController downloadController(downloadService, &notifications);
    UploadController uploadController(uploadService, fileOperationService, &notifications);
    AuthController authController(authService);
    AccountController accountController(accountService);

    // Shared across every tab (Phase 10's side panel is chrome beside the
    // tab content, not per-tab state), same app-lifetime-singleton shape as
    // thumbnailService/notifications above rather than tabFactory below.
    auto folderTreeService = std::make_shared<FolderTreeService>(client);
    FolderTreeModel folderTreeModel(folderTreeService);

    // Same shared side-panel scope as folderTreeService/folderTreeModel above.
    // The store needs no resolved path, unlike sessionStore: it persists
    // through QSettings, which resolves its own location from the
    // organization/application name set at the top of this function.
    auto pinnedFolderStore = std::make_shared<QSettingsPinnedFolderStore>();
    auto quickAccessService = std::make_shared<QuickAccessService>(client, pinnedFolderStore);
    QuickAccessModel quickAccessModel(quickAccessService, &notifications);

    // Wires one tab's worth of navigation/search/thumbnail state: a fresh
    // FolderNavigationService/SearchService/FolderNavigationController/
    // ThumbnailController each, capturing the shared, app-lifetime
    // client/thumbnailService/&notifications above. TabsController calls
    // this whenever a new tab is needed (initial tab, "+", middle-click-open,
    // "Open in new tab") -- it has no wiring knowledge of its own, per
    // docs/ARCHITECTURE.md's composition-root convention.
    auto tabFactory = [client, thumbnailService, fileOperationService, &notifications, &clipboard]()
        -> TabContext {
        auto navigationService = std::make_shared<FolderNavigationService>(client);
        auto searchService = std::make_shared<SearchService>(client, navigationService);
        // makeGuiOwned, not make_shared: an in-flight callback can drop the
        // last reference to either of these from the SDK thread once the tab
        // is closed, and both are QObjects (GuiThread.h explains the rest).
        auto navigation = makeGuiOwned<FolderNavigationController>(
            navigationService, searchService, fileOperationService, &notifications, &clipboard);
        auto thumbnails = makeGuiOwned<ThumbnailController>(
            thumbnailService, navigation->fileListModelForThumbnails(), &notifications);
        return TabContext{std::move(navigationService),
                          std::move(searchService),
                          std::move(navigation),
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

    // Explicit stop point, not left to ~MegaSdkClient. Destroying MegaApi makes
    // the SDK thread fail every pending request/transfer before it joins, and
    // client is declared first here, so it is destroyed *last* -- those
    // callbacks would land on services and controllers that are already gone.
    // Stopping it here, while everything above is still alive, is the whole
    // fix; swapping declaration order cannot achieve it, since a dozen
    // shared_ptrs keep client alive. See REFACTOR_PLANS.md's R2-3.
    client->shutdown();
    return exitCode;
}
