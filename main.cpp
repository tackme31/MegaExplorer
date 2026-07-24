#include "core/FileListingService.h"
#include "mega/MegaSdkClient.h"

#include <QDebug>
#include <QGuiApplication>
#include <QMetaObject>
#include <QString>

#include <memory>

namespace
{

// Login/fetchNodes callbacks fire on an SDK-internal background thread (see
// CLAUDE.md's Design section), so quitting the event loop must go through a
// queued invoke rather than calling qApp->exit() directly from that thread.
void quitFromAnyThread(int code)
{
    QMetaObject::invokeMethod(qApp, [code] { qApp->exit(code); }, Qt::QueuedConnection);
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

    auto client = std::make_shared<MegaSdkClient>();
    FileListingService service(client);

    service.loadRootListing(email, password, [](Result<std::vector<FileEntry>> result) {
        if (!result.success)
        {
            qWarning() << "loadRootListing failed:" << QString::fromStdString(result.errorMessage)
                       << "code=" << result.errorCode;
            quitFromAnyThread(1);
            return;
        }

        for (const FileEntry& entry : result.value)
        {
            qDebug() << (entry.isFolder ? "[dir] " : "[file]")
                     << QString::fromStdString(entry.name)
                     << "size=" << entry.sizeBytes;
        }
        quitFromAnyThread(0);
    });

    return app.exec();
}
