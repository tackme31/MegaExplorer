#include "FolderNavigationController.h"

#include "app/Logging.h"
#include "core/MenuActionResolver.h"
#include "GuiThread.h"
#include "NotificationController.h"
#include "ViewKindEnum.h"

#include <QDebug>
#include <QString>
#include <QVariantMap>

#include <set>

FolderNavigationController::FolderNavigationController(
    std::shared_ptr<FolderNavigationService> navigationService,
    std::shared_ptr<SearchService> searchService,
    std::shared_ptr<BusyState> busy,
    NotificationController* notifications,
    QObject* parent)
    : QObject(parent), mService(std::move(navigationService)),
      mSearchService(std::move(searchService)), mNotifications(notifications),
      mFileListModel(std::make_shared<FileListModel>()), mBusy(std::move(busy))
{
    connect(mBusy.get(), &BusyState::changed, this, &FolderNavigationController::busyChanged);
}

QObject* FolderNavigationController::fileListModel()
{
    return mFileListModel.get();
}

std::shared_ptr<FileListModel> FolderNavigationController::fileListModelForThumbnails()
{
    return mFileListModel;
}

bool FolderNavigationController::canGoBack() const
{
    return mService->canGoBack();
}

bool FolderNavigationController::busy() const
{
    return mBusy->visible();
}

QVariantList FolderNavigationController::breadcrumb() const
{
    return mBreadcrumb;
}

QString FolderNavigationController::currentFolderName() const
{
    if (mBreadcrumb.isEmpty())
        return QString();
    return mBreadcrumb.last().toMap().value(QStringLiteral("name")).toString();
}

bool FolderNavigationController::canGoUp() const
{
    return mBreadcrumb.size() >= 2;
}

bool FolderNavigationController::atRoot() const
{
    if (mBreadcrumb.isEmpty())
        return true;
    return mBreadcrumb.last().toMap().value(QStringLiteral("isRoot")).toBool();
}

quint64 FolderNavigationController::currentHandle() const
{
    if (mBreadcrumb.isEmpty())
        return 0;
    return mBreadcrumb.last().toMap().value(QStringLiteral("handle")).toULongLong();
}

int FolderNavigationController::viewKind() const
{
    if (mBreadcrumb.isEmpty())
        return ViewKindEnum::CloudDrive;
    return mBreadcrumb.last().toMap().value(QStringLiteral("kind")).toInt();
}

void FolderNavigationController::loadRoot()
{
    mService->openRoot(mSortOrder,
                       [this, self = shared_from_this()](Result<std::vector<FileEntry>> result) {
                           invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
                               applyResult(std::move(result));
                           });
                       });
}

void FolderNavigationController::openFolder(quint64 handle)
{
    mService->openFolder(static_cast<std::uint64_t>(handle),
                         mSortOrder,
                         [this, self = shared_from_this()](Result<std::vector<FileEntry>> result) {
                             invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
                                 applyResult(std::move(result));
                             });
                         });
}

void FolderNavigationController::goBack()
{
    if (!canGoBack())
        return;
    mService->goBack(mSortOrder,
                     [this, self = shared_from_this()](Result<std::vector<FileEntry>> result) {
                         invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
                             applyResult(std::move(result));
                         });
                     });
}

void FolderNavigationController::goUp()
{
    if (!canGoUp())
        return;
    const QVariantMap parent = mBreadcrumb.at(mBreadcrumb.size() - 2).toMap();
    navigateTo(parent.value(QStringLiteral("handle")).toULongLong(),
               parent.value(QStringLiteral("isRoot")).toBool());
}

void FolderNavigationController::navigateTo(quint64 handle, bool isRoot)
{
    mService->navigateTo(static_cast<std::uint64_t>(handle),
                         isRoot,
                         mSortOrder,
                         [this, self = shared_from_this()](Result<std::vector<FileEntry>> result) {
                             invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
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
                                    result.errorCode,
                                    QString::fromStdString(result.errorMessage));
        return;
    }
    mHasLoadedOnce = true;
    mLastFolderEntries = result.value();
    mFileListModel->setEntries(std::move(result.value()));
    emit canGoBackChanged();
    refreshBreadcrumb();
}

void FolderNavigationController::refreshBreadcrumb()
{
    mService->resolveCurrentPath(
        [this, self = shared_from_this()](Result<std::vector<PathSegment>> result) {
            invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
                if (!result.success)
                {
                    qCWarning(lcNavigation) << "breadcrumb path resolution failed:"
                                            << QString::fromStdString(result.errorMessage)
                                            << "code=" << result.errorCode;
                    return;
                }

                QVariantList breadcrumb;
                breadcrumb.reserve(static_cast<qsizetype>(result.value().size()));
                for (const PathSegment& segment : result.value())
                {
                    QVariantMap entry;
                    entry.insert(QStringLiteral("name"), QString::fromStdString(segment.name));
                    entry.insert(QStringLiteral("handle"), static_cast<qulonglong>(segment.handle));
                    entry.insert(QStringLiteral("isRoot"), segment.isRoot);
                    entry.insert(QStringLiteral("kind"), static_cast<int>(segment.kind));
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
                publishViewKind();
                emit breadcrumbChanged();
            });
        });
}

void FolderNavigationController::publishViewKind()
{
    mFileListModel->setViewKind(static_cast<ViewKind>(viewKind()));
}

bool FolderNavigationController::canPerform(const QString& actionId) const
{
    const std::string id = actionId.toStdString();
    const ViewKind kind = static_cast<ViewKind>(viewKind());

    // A shortcut stands in for a row of one of the two menus a file view can open --
    // the selection's and the background's -- so either offering it is enough.
    const MenuContext selectionContext{
        kind, MenuSite::FileSelection, mFileListModel->selectionSummary()};
    return menuActionAllowed(id, selectionContext) ||
           menuActionAllowed(id, folderTargetContext(MenuSite::FolderBackground, kind));
}

void FolderNavigationController::search(QString query)
{
    mLastSearchQuery = query.toStdString();

    if (query.isEmpty())
    {
        mFileListModel->setEntries(mLastFolderEntries);
        return;
    }

    mSearchService->search(
        mLastSearchQuery,
        mSortOrder,
        [this, self = shared_from_this()](Result<std::vector<FileEntry>> result) {
            invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
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
                                    result.errorCode,
                                    QString::fromStdString(result.errorMessage));
        return;
    }
    mFileListModel->setEntries(std::move(result.value()));
}

void FolderNavigationController::refreshCurrentFolder()
{
    mService->refreshCurrent(
        mSortOrder, [this, self = shared_from_this()](Result<std::vector<FileEntry>> result) {
            invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
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

    refreshVisibleListing();
}

void FolderNavigationController::refreshVisibleListing()
{
    if (!mLastSearchQuery.empty())
    {
        // Re-run the visible search, and separately refresh the cached folder
        // listing (not the visible model) so that clearing the search
        // afterwards doesn't show stale contents/ordering.
        mSearchService->search(
            mLastSearchQuery,
            mSortOrder,
            [this, self = shared_from_this()](Result<std::vector<FileEntry>> result) {
                invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
                    applySearchResult(std::move(result));
                });
            });
        mService->refreshCurrent(
            mSortOrder, [this, self = shared_from_this()](Result<std::vector<FileEntry>> result) {
                invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
                    if (!result.success)
                    {
                        // No toast: the visible model is the search result,
                        // which reports its own failure. This one only means
                        // the cache stays stale, so clearing the search shows
                        // the pre-refresh listing.
                        qCWarning(lcNavigation) << "background folder refresh failed:"
                                                << QString::fromStdString(result.errorMessage)
                                                << "code=" << result.errorCode;
                        return;
                    }
                    mLastFolderEntries = std::move(result.value());
                });
            });
        return;
    }

    refreshCurrentFolder();
}

void FolderNavigationController::refreshListingIfLoaded()
{
    if (!mHasLoadedOnce)
        return;
    refreshVisibleListing();
}

bool FolderNavigationController::isLoaded() const
{
    return mHasLoadedOnce;
}

std::set<std::string> FolderNavigationController::cachedChildNames() const
{
    std::set<std::string> names;
    for (const FileEntry& entry : mLastFolderEntries)
        names.insert(entry.name);
    return names;
}

void FolderNavigationController::applyFavouriteChange(quint64 handle, bool favourite)
{
    mFileListModel->setFavourite(handle, favourite);
    for (FileEntry& entry : mLastFolderEntries)
    {
        if (entry.handle == static_cast<std::uint64_t>(handle))
        {
            entry.isFavourite = favourite;
            break;
        }
    }
}

void FolderNavigationController::refresh()
{
    if (!mHasLoadedOnce)
        return;

    mBusy->begin();
    mService->syncWithServer([this, self = shared_from_this()](Result<void> result) {
        invokeOnGuiThread(this, [this, result = std::move(result)]() {
            mBusy->end();
            if (!result.success)
            {
                qCWarning(lcNavigation)
                    << "server sync failed:" << QString::fromStdString(result.errorMessage)
                    << "code=" << result.errorCode;
                mNotifications->notifyError(QStringLiteral("refresh"),
                                            result.errorCode,
                                            QString::fromStdString(result.errorMessage));
            }
            // Re-read either way. The user asked for the folder to be
            // refreshed; failing to reach the API is worth a toast, but it is
            // no reason to withhold what the SDK already has.
            refreshListingIfLoaded();
        });
    });
}

void FolderNavigationController::refreshIfShowing(quint64 handle, bool isRoot)
{
    if (atRoot() != isRoot)
        return;
    if (!isRoot && currentHandle() != handle)
        return;
    refreshListingIfLoaded();
}

void FolderNavigationController::reset()
{
    mService->resetToRoot();
    mFileListModel->setEntries({});
    mLastFolderEntries.clear();
    mLastSearchQuery.clear();
    mHasLoadedOnce = false;
    mBreadcrumb.clear();
    publishViewKind();
    mBusy->abandonAll();
    emit canGoBackChanged();
    emit breadcrumbChanged();
}
