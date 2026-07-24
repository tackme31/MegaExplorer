#include "core/FileListingService.h"
#include "core/FolderNavigationService.h"
#include "mega/MegaSdkClient.h"
#include "qml/FolderNavigationController.h"

#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include <memory>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

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
    FolderNavigationController controller(navigationService, listingService);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("controller", &controller);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("MegaExplorer", "Main");

    controller.loadRoot(email, password);

    return app.exec();
}
