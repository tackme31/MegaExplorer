#include "mega/MegaSdkClient.h"

#include <QDebug>
#include <QGuiApplication>
#include <QMetaObject>
#include <QString>

#include <cstdlib>
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

    const char* email = std::getenv("MEGA_EMAIL");
    const char* password = std::getenv("MEGA_PWD");
    if (!email || !password)
    {
        qWarning() << "MEGA_EMAIL / MEGA_PWD environment variables must be set";
        return 1;
    }

    auto client = std::make_shared<MegaSdkClient>();

    client->login(email, password, [client](Result<void> loginResult) {
        if (!loginResult.success)
        {
            qWarning() << "login failed:" << QString::fromStdString(loginResult.errorMessage)
                       << "code=" << loginResult.errorCode;
            quitFromAnyThread(1);
            return;
        }
        qDebug() << "login ok";

        client->fetchNodes([client](Result<void> fetchResult) {
            if (!fetchResult.success)
            {
                qWarning() << "fetchNodes failed:" << QString::fromStdString(fetchResult.errorMessage)
                           << "code=" << fetchResult.errorCode;
                quitFromAnyThread(1);
                return;
            }
            qDebug() << "fetchNodes ok";

            client->getRootChildren([](Result<std::vector<FileEntry>> childrenResult) {
                if (!childrenResult.success)
                {
                    qWarning() << "getRootChildren failed:"
                               << QString::fromStdString(childrenResult.errorMessage)
                               << "code=" << childrenResult.errorCode;
                    quitFromAnyThread(1);
                    return;
                }

                for (const FileEntry& entry : childrenResult.value)
                {
                    qDebug() << (entry.isFolder ? "[dir] " : "[file]")
                             << QString::fromStdString(entry.name)
                             << "size=" << entry.sizeBytes;
                }
                quitFromAnyThread(0);
            });
        });
    });

    return app.exec();
}
