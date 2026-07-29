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

    auto navigationService = std::make_shared<FolderNavigationService>(client);
    auto searchService = std::make_shared<SearchService>(client, navigationService);
    auto downloadService = std::make_shared<DownloadService>(client);
    auto thumbnailService = std::make_shared<ThumbnailService>(client);
    auto authService = std::make_shared<AuthService>(client, sessionStore);
    // Declared before the controllers below: they hold a non-owning pointer
    // to it, and stack locals are destroyed in reverse construction order.
    NotificationController notifications;
    FolderNavigationController controller(navigationService, searchService, &notifications);
    DownloadController downloadController(downloadService, &notifications);
    ThumbnailController thumbnailController(
        thumbnailService, controller.fileListModelForThumbnails(), &notifications);
    AuthController authController(authService);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("controller", &controller);
    engine.rootContext()->setContextProperty("downloadController", &downloadController);
    engine.rootContext()->setContextProperty("thumbnailController", &thumbnailController);
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
