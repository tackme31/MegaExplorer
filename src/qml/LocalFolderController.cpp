#include "LocalFolderController.h"

#include "app/Logging.h"
#include "GuiThread.h"
#include "NotificationController.h"

#include <QDesktopServices>
#include <QDir>
#include <QProcess>
#include <QUrl>

#include <utility>

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
    withLocalPath(handle, QStringLiteral("openLocalLocation"), [this](const QString& path) {
        if (!revealInExplorer(path))
        {
            qCWarning(lcApp) << "failed to start explorer.exe for" << path;
            mNotifications->notifyError(QStringLiteral("openLocalLocation"));
        }
    });
}

void LocalFolderController::openFile(quint64 handle)
{
    withLocalPath(handle, QStringLiteral("openLocalFile"), [this](const QString& path) {
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        {
            qCWarning(lcApp) << "failed to open local file:" << path;
            mNotifications->notifyError(QStringLiteral("openFile"));
        }
    });
}

void LocalFolderController::withLocalPath(quint64 handle,
                                          QString resolveFailToast,
                                          std::function<void(const QString&)> act)
{
    mService->resolveLocalPath(
        static_cast<std::uint64_t>(handle),
        mLocalRoot.toStdString(),
        [this, resolveFailToast = std::move(resolveFailToast), act = std::move(act)](
            Result<std::string> result) {
            invokeOnGuiThread(
                this,
                [this, resolveFailToast, act, result = std::move(result)] {
                    if (!result.success)
                    {
                        qCWarning(lcApp) << "no local counterpart for this item:"
                                         << QString::fromStdString(result.errorMessage);
                        mNotifications->notifyError(resolveFailToast);
                        return;
                    }
                    act(QString::fromStdString(result.value()));
                });
        });
}
