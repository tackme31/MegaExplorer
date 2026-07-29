#include "app/Logging.h"
#include "core/AuthService.h"
#include "core/DownloadService.h"
#include "core/FolderNavigationService.h"
#include "core/SearchService.h"
#include "core/ThumbnailService.h"
#include "mega/MegaSdkClient.h"
#include "platform/WindowsSessionStore.h"
#include "qml/AuthController.h"
#include "qml/DownloadController.h"
#include "qml/FolderNavigationController.h"
#include "qml/NotificationController.h"
#include "qml/TabsController.h"
#include "qml/ThumbnailController.h"

#include <QDebug>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>

#include <memory>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    // QML Settings (view mode persistence) needs these to resolve a
    // per-app registry/config location; without them QSettings fails to
    // initialize (Status code 1) and every read/write silently no-ops.
    QCoreApplication::setOrganizationName("MegaExplorer");
    QCoreApplication::setApplicationName("MegaExplorer");

    // Must run before any other logging call: appMegaExplorer is
    // WIN32_EXECUTABLE, so qWarning()/qCWarning() output reaches no visible
    // destination at all on a normal launch until this installs a file sink.
    installLogging();

    auto client = std::make_shared<MegaSdkClient>();

    // AppLocalDataLocation, not AppDataLocation: same non-roaming rationale
    // as Logging.cpp's log file (see installLogging()).
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(cacheDir);
    auto sessionStore =
        std::make_shared<WindowsSessionStore>((cacheDir + "/session.dat").toStdString());

    auto downloadService = std::make_shared<DownloadService>(client);
    // Shared across every tab (handle-keyed cache, no per-tab state) --
    // unlike FolderNavigationService/SearchService/FolderNavigationController
    // below, which are inherently per-tab and so live in tabFactory instead.
    auto thumbnailService = std::make_shared<ThumbnailService>(client);
    auto authService = std::make_shared<AuthService>(client, sessionStore);
    // Declared before the controllers below: they hold a non-owning pointer
    // to it, and stack locals are destroyed in reverse construction order.
    NotificationController notifications;
    DownloadController downloadController(downloadService, &notifications);
    AuthController authController(authService);

    // Wires one tab's worth of navigation/search/thumbnail state: a fresh
    // FolderNavigationService/SearchService/FolderNavigationController/
    // ThumbnailController each, capturing the shared, app-lifetime
    // client/thumbnailService/&notifications above. TabsController calls
    // this whenever a new tab is needed (initial tab, "+", middle-click-open,
    // "Open in new tab") -- it has no wiring knowledge of its own, per
    // docs/ARCHITECTURE.md's composition-root convention.
    auto tabFactory = [client, thumbnailService, &notifications]() -> TabContext {
        auto navigationService = std::make_shared<FolderNavigationService>(client);
        auto searchService = std::make_shared<SearchService>(client, navigationService);
        auto navigation = std::make_shared<FolderNavigationController>(
            navigationService, searchService, &notifications);
        auto thumbnails = std::make_shared<ThumbnailController>(
            thumbnailService, navigation->fileListModelForThumbnails(), &notifications);
        return TabContext{std::move(navigationService),
                          std::move(searchService),
                          std::move(navigation),
                          std::move(thumbnails)};
    };
    TabsController tabs(tabFactory);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("tabsController", &tabs);
    engine.rootContext()->setContextProperty("downloadController", &downloadController);
    engine.rootContext()->setContextProperty("notificationController", &notifications);
    engine.rootContext()->setContextProperty("authController", &authController);
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

    return app.exec();
}
