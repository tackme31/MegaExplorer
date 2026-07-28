#include "app/Logging.h"
#include "core/DownloadService.h"
#include "core/FileListingService.h"
#include "core/FolderNavigationService.h"
#include "core/SearchService.h"
#include "core/ThumbnailService.h"
#include "mega/MegaSdkClient.h"
#include "platform/SqliteNodeCache.h"
#include "qml/DownloadController.h"
#include "qml/FolderNavigationController.h"
#include "qml/NotificationController.h"
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

    if (!qEnvironmentVariableIsSet("MEGA_EMAIL") || !qEnvironmentVariableIsSet("MEGA_PWD"))
    {
        qCWarning(lcApp) << "MEGA_EMAIL / MEGA_PWD environment variables must be set";
        return 1;
    }
    const std::string email = qEnvironmentVariable("MEGA_EMAIL").toStdString();
    const std::string password = qEnvironmentVariable("MEGA_PWD").toStdString();

    auto client = std::make_shared<MegaSdkClient>();

    // AppLocalDataLocation, not AppDataLocation: same non-roaming rationale
    // as Logging.cpp's log file (see installLogging()).
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(cacheDir);
    auto nodeCache =
        std::make_shared<SqliteNodeCache>((cacheDir + "/node_cache.sqlite3").toStdString());

    // navigationService must exist before listingService: the latter
    // delegates its final root-listing step to
    // FolderNavigationService::openRoot (Phase 6) instead of calling
    // IMegaClient::getRootChildren directly.
    auto navigationService = std::make_shared<FolderNavigationService>(client, nodeCache);
    auto listingService = std::make_shared<FileListingService>(client, navigationService);
    auto searchService = std::make_shared<SearchService>(client, navigationService);
    auto downloadService = std::make_shared<DownloadService>(client);
    auto thumbnailService = std::make_shared<ThumbnailService>(client);
    // Declared before the controllers below: they hold a non-owning pointer
    // to it, and stack locals are destroyed in reverse construction order.
    NotificationController notifications;
    FolderNavigationController controller(
        navigationService, listingService, searchService, &notifications);
    DownloadController downloadController(downloadService, &notifications);
    ThumbnailController thumbnailController(
        thumbnailService, controller.fileListModelForThumbnails(), &notifications);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("controller", &controller);
    engine.rootContext()->setContextProperty("downloadController", &downloadController);
    engine.rootContext()->setContextProperty("thumbnailController", &thumbnailController);
    engine.rootContext()->setContextProperty("notificationController", &notifications);
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [] {
            QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);
    engine.loadFromModule("MegaExplorer", "Main");

    controller.loadRoot(email, password);

    return app.exec();
}
