#include "FolderNavigationController.h"

#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>
#include <QString>

namespace
{

// Service callbacks may fire on an SDK-internal background thread (see
// IMegaClient.h), so touching the QML-facing model/property from there must
// go through a queued invoke onto the GUI thread. Same idiom as main.cpp's
// own invokeOnGuiThread; duplicated here rather than shared since it's a
// trivial 3-line, stateless helper.
void invokeOnGuiThread(std::function<void()> fn)
{
    QMetaObject::invokeMethod(qApp, std::move(fn), Qt::QueuedConnection);
}

} // namespace

FolderNavigationController::FolderNavigationController(
    std::shared_ptr<FolderNavigationService> navigationService,
    std::shared_ptr<FileListingService> listingService,
    std::shared_ptr<SearchService> searchService,
    QObject* parent)
    : QObject(parent), mService(std::move(navigationService)),
      mListingService(std::move(listingService)), mSearchService(std::move(searchService))
{}

QObject* FolderNavigationController::fileListModel()
{
    return &mFileListModel;
}

FileListModel* FolderNavigationController::fileListModelForThumbnails()
{
    return &mFileListModel;
}

bool FolderNavigationController::canGoBack() const
{
    return mService->canGoBack();
}

void FolderNavigationController::loadRoot(const std::string& email, const std::string& password)
{
    mListingService->loadRootListing(
        email, password, [this](Result<std::vector<FileEntry>> result) {
            invokeOnGuiThread([this, result = std::move(result)]() mutable {
                applyResult(std::move(result));
            });
        });
}

void FolderNavigationController::openFolder(quint64 handle)
{
    mService->openFolder(static_cast<std::uint64_t>(handle),
                         [this](Result<std::vector<FileEntry>> result) {
                             invokeOnGuiThread([this, result = std::move(result)]() mutable {
                                 applyResult(std::move(result));
                             });
                         });
}

void FolderNavigationController::goBack()
{
    mService->goBack([this](Result<std::vector<FileEntry>> result) {
        invokeOnGuiThread([this, result = std::move(result)]() mutable {
            applyResult(std::move(result));
        });
    });
}

void FolderNavigationController::applyResult(Result<std::vector<FileEntry>> result)
{
    if (!result.success)
    {
        qWarning() << "folder navigation failed:" << QString::fromStdString(result.errorMessage)
                   << "code=" << result.errorCode;
        return;
    }
    mLastFolderEntries = result.value;
    mFileListModel.setEntries(std::move(result.value));
    emit canGoBackChanged();
}

void FolderNavigationController::search(QString query)
{
    if (query.isEmpty())
    {
        mFileListModel.setEntries(mLastFolderEntries);
        return;
    }

    mSearchService->search(query.toStdString(), [this](Result<std::vector<FileEntry>> result) {
        invokeOnGuiThread([this, result = std::move(result)]() mutable {
            applySearchResult(std::move(result));
        });
    });
}

void FolderNavigationController::applySearchResult(Result<std::vector<FileEntry>> result)
{
    if (!result.success)
    {
        qWarning() << "search failed:" << QString::fromStdString(result.errorMessage)
                   << "code=" << result.errorCode;
        return;
    }
    mFileListModel.setEntries(std::move(result.value));
}
