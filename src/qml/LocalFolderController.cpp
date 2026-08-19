#include "LocalFolderController.h"

#include "app/Logging.h"
#include "GuiThread.h"
#include "NotificationController.h"

#include <QDir>
#include <QProcess>

namespace
{

bool revealInExplorer(const QString& nativePath)
{
    QProcess explorer;
    explorer.setProgram(QStringLiteral("explorer.exe"));
    // Native arguments rather than setArguments(): explorer.exe parses its own
    // command line, and both of QProcess' quotings of a "/select,<path>" argument
    // list are wrong for it -- one token gets quoted from before the switch, two
    // tokens put a space between the switch and the path. The path is quoted here
    // instead, which needs no escaping since Windows paths cannot contain '"'.
    explorer.setNativeArguments(QStringLiteral("/select,\"") + nativePath + QStringLiteral("\""));
    return explorer.startDetached();
}

} // namespace

LocalFolderController::LocalFolderController(std::shared_ptr<LocalLinkService> service,
                                             NotificationController* notifications,
                                             QObject* parent)
    : QObject(parent), mService(std::move(service)), mNotifications(notifications)
{}

QString LocalFolderController::localRoot() const
{
    return mLocalRoot;
}

void LocalFolderController::setLocalRoot(QString path)
{
    if (mLocalRoot == path)
        return;
    mLocalRoot = std::move(path);
    emit localRootChanged();
}

bool LocalFolderController::linked() const
{
    return !mLocalRoot.isEmpty();
}

QString LocalFolderController::pathFromUrl(const QUrl& url) const
{
    const QString path = url.toLocalFile();
    // Native separators from here on: the joined path is built with '\' in
    // LocalLinkService, which links no Qt and cannot normalize a mixed one.
    return path.isEmpty() ? path : QDir::toNativeSeparators(path);
}

void LocalFolderController::openLocation(quint64 handle)
{
    mService->resolveLocalPath(
        static_cast<std::uint64_t>(handle),
        mLocalRoot.toStdString(),
        [this](Result<std::string> result) {
            invokeOnGuiThread(this, [this, result = std::move(result)] {
                if (!result.success)
                {
                    qCWarning(lcApp) << "no local location for this item:"
                                     << QString::fromStdString(result.errorMessage);
                    mNotifications->notifyError(QStringLiteral("openLocalLocation"));
                    return;
                }
                if (!revealInExplorer(QString::fromStdString(result.value())))
                {
                    qCWarning(lcApp) << "failed to start explorer.exe for"
                                     << QString::fromStdString(result.value());
                    mNotifications->notifyError(QStringLiteral("openLocalLocation"));
                }
            });
        });
}
