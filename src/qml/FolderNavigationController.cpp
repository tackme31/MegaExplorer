#include "FolderNavigationController.h"

#include "app/Logging.h"
#include "NotificationController.h"

#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>
#include <QString>
#include <QVariantMap>

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
    std::shared_ptr<SearchService> searchService,
    NotificationController* notifications,
    QObject* parent)
    : QObject(parent), mService(std::move(navigationService)),
      mSearchService(std::move(searchService)), mNotifications(notifications)
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

QVariantList FolderNavigationController::breadcrumb() const
{
    return mBreadcrumb;
}

void FolderNavigationController::loadRoot()
{
    mService->openRoot(mSortOrder, [this](Result<std::vector<FileEntry>> result) {
        invokeOnGuiThread([this, result = std::move(result)]() mutable {
            applyResult(std::move(result));
        });
    });
}

void FolderNavigationController::openFolder(quint64 handle)
{
    mService->openFolder(static_cast<std::uint64_t>(handle),
                         mSortOrder,
                         [this](Result<std::vector<FileEntry>> result) {
                             invokeOnGuiThread([this, result = std::move(result)]() mutable {
                                 applyResult(std::move(result));
                             });
                         });
}

void FolderNavigationController::goBack()
{
    mService->goBack(mSortOrder, [this](Result<std::vector<FileEntry>> result) {
        invokeOnGuiThread([this, result = std::move(result)]() mutable {
            applyResult(std::move(result));
        });
    });
}

void FolderNavigationController::navigateTo(quint64 handle, bool isRoot)
{
    mService->navigateTo(static_cast<std::uint64_t>(handle),
                         isRoot,
                         mSortOrder,
                         [this](Result<std::vector<FileEntry>> result) {
                             invokeOnGuiThread([this, result = std::move(result)]() mutable {
                                 applyResult(std::move(result));
                             });
                         });
}

void FolderNavigationController::applyResult(Result<std::vector<FileEntry>> result)
{
    if (!result.success)
    {
        qCWarning(lcNavigation) << "folder navigation failed:"
                                << QString::fromStdString(result.errorMessage)
                                << "code=" << result.errorCode;
        mNotifications->notifyError(QStringLiteral("navigation"),
                                    QString::fromStdString(result.errorMessage));
        return;
    }
    mHasLoadedOnce = true;
    mLastFolderEntries = result.value;
    mFileListModel.setEntries(std::move(result.value));
    emit canGoBackChanged();
    refreshBreadcrumb();
}

void FolderNavigationController::refreshBreadcrumb()
{
    mService->resolveCurrentPath([this](Result<std::vector<PathSegment>> result) {
        invokeOnGuiThread([this, result = std::move(result)]() mutable {
            if (!result.success)
            {
                qCWarning(lcNavigation)
                    << "breadcrumb path resolution failed:"
                    << QString::fromStdString(result.errorMessage) << "code=" << result.errorCode;
                return;
            }

            QVariantList breadcrumb;
            breadcrumb.reserve(static_cast<qsizetype>(result.value.size()));
            for (const PathSegment& segment : result.value)
            {
                QVariantMap entry;
                entry.insert(QStringLiteral("name"), QString::fromStdString(segment.name));
                entry.insert(QStringLiteral("handle"), static_cast<qulonglong>(segment.handle));
                entry.insert(QStringLiteral("isRoot"), segment.isRoot);
                breadcrumb.append(entry);
            }

            // A QVariantList model has no diffing on the QML side, so a
            // Repeater over it rebuilds every delegate on each emit --
            // skip the (re-)assignment entirely when the resolved path is
            // unchanged (e.g. refreshCurrentFolder() re-running this after a
            // sort-order change while staying in the same folder).
            if (breadcrumb == mBreadcrumb)
                return;
            mBreadcrumb = std::move(breadcrumb);
            emit breadcrumbChanged();
        });
    });
}

void FolderNavigationController::search(QString query)
{
    mLastSearchQuery = query.toStdString();

    if (query.isEmpty())
    {
        mFileListModel.setEntries(mLastFolderEntries);
        return;
    }

    mSearchService->search(
        mLastSearchQuery, mSortOrder, [this](Result<std::vector<FileEntry>> result) {
            invokeOnGuiThread([this, result = std::move(result)]() mutable {
                applySearchResult(std::move(result));
            });
        });
}

void FolderNavigationController::applySearchResult(Result<std::vector<FileEntry>> result)
{
    if (!result.success)
    {
        qCWarning(lcSearch) << "search failed:" << QString::fromStdString(result.errorMessage)
                            << "code=" << result.errorCode;
        mNotifications->notifyError(QStringLiteral("search"),
                                    QString::fromStdString(result.errorMessage));
        return;
    }
    mFileListModel.setEntries(std::move(result.value));
}

void FolderNavigationController::refreshCurrentFolder()
{
    mService->refreshCurrent(mSortOrder, [this](Result<std::vector<FileEntry>> result) {
        invokeOnGuiThread([this, result = std::move(result)]() mutable {
            applyResult(std::move(result));
        });
    });
}

void FolderNavigationController::setSortOrder(int column, bool ascending)
{
    switch (column)
    {
        case 1:
            mSortOrder.key = SortKey::ModificationTime;
            break;
        case 2:
            mSortOrder.key = SortKey::Size;
            break;
        case 0:
        default:
            mSortOrder.key = SortKey::Name;
            break;
    }
    mSortOrder.ascending = ascending;

    // Called once at startup with the Settings-restored value, before
    // loadRoot() has ever run (see mHasLoadedOnce's declaration) -- just
    // record the order in that case, don't fetch yet.
    if (!mHasLoadedOnce)
        return;

    if (!mLastSearchQuery.empty())
    {
        // Re-run the visible search under the new order, and separately
        // refresh the cached folder listing (not the visible model) so that
        // clearing the search afterwards doesn't show stale ordering.
        mSearchService->search(
            mLastSearchQuery, mSortOrder, [this](Result<std::vector<FileEntry>> result) {
                invokeOnGuiThread([this, result = std::move(result)]() mutable {
                    applySearchResult(std::move(result));
                });
            });
        mService->refreshCurrent(mSortOrder, [this](Result<std::vector<FileEntry>> result) {
            invokeOnGuiThread([this, result = std::move(result)]() mutable {
                if (result.success)
                    mLastFolderEntries = std::move(result.value);
            });
        });
        return;
    }

    refreshCurrentFolder();
}

void FolderNavigationController::reset()
{
    mService->resetToRoot();
    mFileListModel.setEntries({});
    mLastFolderEntries.clear();
    mLastSearchQuery.clear();
    mHasLoadedOnce = false;
    mBreadcrumb.clear();
    emit canGoBackChanged();
    emit breadcrumbChanged();
}
