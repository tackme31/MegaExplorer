#include "PropertiesController.h"

#include "app/Logging.h"
#include "GuiThread.h"

#include <QLocale>

#include <utility>

PropertiesController::PropertiesController(std::shared_ptr<NodeDetailsService> service,
                                           QObject* parent)
    : QObject(parent), mService(std::move(service))
{}

QString PropertiesController::name() const
{
    return mName;
}

bool PropertiesController::isFolder() const
{
    return mIsFolder;
}

qulonglong PropertiesController::sizeBytes() const
{
    return mSizeBytes;
}

QString PropertiesController::formattedSize() const
{
    // c() rather than system(): the unit word comes from locale data, not from a
    // translation, so a Japanese system prints "bytes" in Japanese into a UI that
    // is English everywhere else.
    return QLocale::c().formattedDataSize(static_cast<qint64>(mSizeBytes),
                                          1,
                                          QLocale::DataSizeTraditionalFormat);
}

qlonglong PropertiesController::modificationTime() const
{
    return mModificationTime;
}

QString PropertiesController::parentPath() const
{
    return mParentPath;
}

int PropertiesController::rootKind() const
{
    return mRootKind;
}

int PropertiesController::fileCount() const
{
    return mFileCount;
}

int PropertiesController::folderCount() const
{
    return mFolderCount;
}

bool PropertiesController::loading() const
{
    return mLoading;
}

bool PropertiesController::failed() const
{
    return mFailed;
}

void PropertiesController::show(quint64 handle,
                                const QString& name,
                                bool isFolder,
                                qulonglong sizeBytes,
                                qlonglong modificationTime)
{
    ++mRequestId;
    const std::uint64_t requestId = mRequestId;

    mName = name;
    mIsFolder = isFolder;
    // A folder's row carries no meaningful size, so start at 0 and let the reply
    // fill in the recursive total.
    mSizeBytes = isFolder ? 0 : sizeBytes;
    mModificationTime = modificationTime;
    mParentPath.clear();
    mRootKind = static_cast<int>(ViewKind::CloudDrive);
    mFileCount = -1;
    mFolderCount = -1;
    mLoading = true;
    mFailed = false;
    emit changed();
    emit showRequested();

    mService->loadDetails(
        static_cast<std::uint64_t>(handle),
        isFolder,
        [this, requestId](Result<NodeDetails> result) {
            invokeOnGuiThread(this, [this, requestId, result = std::move(result)]() mutable {
                if (requestId != mRequestId)
                    return;

                mLoading = false;
                if (!result.success)
                {
                    qCWarning(lcApp) << "node details lookup failed:"
                                     << QString::fromStdString(result.errorMessage)
                                     << "code=" << result.errorCode;
                    mFailed = true;
                    emit changed();
                    return;
                }

                const NodeDetails& details = result.value();
                mParentPath = QString::fromStdString(details.parentPath);
                mRootKind = static_cast<int>(details.rootKind);
                if (details.hasContents)
                {
                    mSizeBytes = static_cast<qulonglong>(details.contents.sizeBytes);
                    mFileCount = static_cast<int>(details.contents.fileCount);
                    mFolderCount = static_cast<int>(details.contents.folderCount);
                }
                emit changed();
            });
        });
}
