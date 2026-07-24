#include "core/FileListingService.h"
#include "mega/MegaSdkClient.h"
#include "qml/FileListModel.h"

#include <QDebug>
#include <QGuiApplication>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QString>

#include <functional>
#include <memory>

namespace
{

// Login/fetchNodes callbacks fire on an SDK-internal background thread (see
// CLAUDE.md's Design section), so touching QGuiApplication/QML-facing state
// from there must go through a queued invoke onto the GUI thread.
void invokeOnGuiThread(std::function<void()> fn)
{
    QMetaObject::invokeMethod(qApp, std::move(fn), Qt::QueuedConnection);
}

}

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

    FileListModel fileListModel;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("fileListModel", &fileListModel);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("MegaExplorer", "Main");

    auto client = std::make_shared<MegaSdkClient>();
    FileListingService service(client);

    service.loadRootListing(email, password, [&fileListModel](Result<std::vector<FileEntry>> result) {
        invokeOnGuiThread([&fileListModel, result = std::move(result)]() mutable {
            if (!result.success)
            {
                qWarning() << "loadRootListing failed:" << QString::fromStdString(result.errorMessage)
                           << "code=" << result.errorCode;
                return;
            }
            fileListModel.setEntries(std::move(result.value));
        });
    });

    return app.exec();
}
