#include "core/DownloadService.h"
#include "core/FileListingService.h"
#include "core/FolderNavigationService.h"
#include "core/SearchService.h"
#include "core/ThumbnailService.h"
#include "mega/MegaSdkClient.h"
#include "qml/DownloadController.h"
#include "qml/FolderNavigationController.h"
#include "qml/ThumbnailController.h"

#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include <memory>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    // QML Settings (view mode persistence) needs these to resolve a
    // per-app registry/config location; without them QSettings fails to
    // initialize (Status code 1) and every read/write silently no-ops.
    QCoreApplication::setOrganizationName("MegaExplorer");
    QCoreApplication::setApplicationName("MegaExplorer");

    if (!qEnvironmentVariableIsSet("MEGA_EMAIL") || !qEnvironmentVariableIsSet("MEGA_PWD"))
    {
        qWarning() << "MEGA_EMAIL / MEGA_PWD environment variables must be set";
        return 1;
    }
    const std::string email = qEnvironmentVariable("MEGA_EMAIL").toStdString();
    const std::string password = qEnvironmentVariable("MEGA_PWD").toStdString();

    auto client = std::make_shared<MegaSdkClient>();
    auto listingService = std::make_shared<FileListingService>(client);
    auto navigationService = std::make_shared<FolderNavigationService>(client);
    auto searchService = std::make_shared<SearchService>(client, navigationService);
    auto downloadService = std::make_shared<DownloadService>(client);
    auto thumbnailService = std::make_shared<ThumbnailService>(client);
    FolderNavigationController controller(navigationService, listingService, searchService);
    DownloadController downloadController(downloadService);
    ThumbnailController thumbnailController(thumbnailService,
                                            controller.fileListModelForThumbnails());

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("controller", &controller);
    engine.rootContext()->setContextProperty("downloadController", &downloadController);
    engine.rootContext()->setContextProperty("thumbnailController", &thumbnailController);
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
