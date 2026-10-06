#include "LocalFolderController.h"

#include "app/Logging.h"
#include "GuiThread.h"
#include "NotificationController.h"

#include <QDesktopServices>
#include <QDir>
#include <QProcess>
#include <QUrl>

#include <utility>
#include <windows.h>
#include <shlobj.h>

namespace
{

bool selectWithShell(const QString& nativePath)
{
    // Qt's GUI thread is normally already an STA; S_FALSE/RPC_E_CHANGED_MODE just mean
    // COM is usable as it stands, and only a successful call may be balanced.
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    PIDLIST_ABSOLUTE pidl = nullptr;
    HRESULT hr = SHParseDisplayName(reinterpret_cast<PCWSTR>(nativePath.utf16()), nullptr, &pidl,
                                    0, nullptr);
    if (SUCCEEDED(hr))
    {
        hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        ILFree(pidl);
    }
    if (SUCCEEDED(init))
        CoUninitialize();
    return SUCCEEDED(hr);
}

// Unlike the shell call, explorer.exe /select always opens a new window and can lose
// the selection when it lands before the folder has finished enumerating.
bool selectWithExplorerExe(const QString& nativePath)
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

bool revealInExplorer(const QString& nativePath)
{
    if (selectWithShell(nativePath))
        return true;
    qCWarning(lcApp) << "SHOpenFolderAndSelectItems failed, falling back to explorer.exe for"
                     << nativePath;
    return selectWithExplorerExe(nativePath);
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
            invokeOnGuiThread(this, [this, resolveFailToast, act, result = std::move(result)] {
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
