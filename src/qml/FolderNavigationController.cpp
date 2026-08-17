#include "FolderNavigationController.h"

#include "app/Logging.h"
#include "core/MenuActionResolver.h"
#include "GuiThread.h"
#include "NotificationController.h"
#include "ViewKindEnum.h"

#include <QDebug>
#include <QString>
#include <QVariantMap>

#include <algorithm>
#include <set>
#include <utility>

namespace
{

// QML hands the filter facets over as ints, so a value outside the enum is reachable
// from a typo in a binding. Every one of these enums starts at "Any", so falling back
// to E{} means "this facet narrows nothing" rather than a garbage cast.
template<typename E>
E clampEnum(int value, E last)
{
    return (value >= 0 && value <= static_cast<int>(last)) ? static_cast<E>(value) : E{};
}

} // namespace

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

bool FolderNavigationController::searchActive() const
{
    return !mLastSearchQuery.empty() || !mSearchFilter.isDefault();
}

void FolderNavigationController::loadRoot()
{
    dropSearchForNavigation();
    mService->openRoot(mSortOrder,
                       [this, self = shared_from_this()](Result<std::vector<FileEntry>> result) {
                           invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
                               applyResult(std::move(result));
                           });
                       });
}

void FolderNavigationController::openFolder(quint64 handle)
{
    dropSearchForNavigation();
    mService->openFolder(static_cast<std::uint64_t>(handle),
                         mSortOrder,
                         [this, self = shared_from_this()](Result<std::vector<FileEntry>> result) {
                             invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
                                 applyResult(std::move(result));
                             });
                         });
}

void FolderNavigationController::openFavourites()
{
    dropSearchForNavigation();
    mService->openFavourites(mSortOrder,
                             [this, self = shared_from_this()](
                                 Result<std::vector<FileEntry>> result) {
                                 invokeOnGuiThread(this,
                                                   [this, result = std::move(result)]() mutable {
                                                       applyResult(std::move(result));
                                                   });
                             });
}

void FolderNavigationController::openRubbish()
{
    dropSearchForNavigation();
    mService->openRubbish(mSortOrder,
                          [this, self = shared_from_this()](
                              Result<std::vector<FileEntry>> result) {
                              invokeOnGuiThread(this,
                                                [this, result = std::move(result)]() mutable {
                                                    applyResult(std::move(result));
                                                });
                          });
}

void FolderNavigationController::goBack()
{
    if (!canGoBack())
        return;
    dropSearchForNavigation();
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
    // The parent's own kind, not this screen's: going up out of a folder inside the
    // Rubbish bin has to land on the bin's top, and its segment is the only thing
    // that says so -- isRoot alone reads as the Cloud Drive root.
    navigateToKind(parent.value(QStringLiteral("handle")).toULongLong(),
                   parent.value(QStringLiteral("isRoot")).toBool(),
                   static_cast<ViewKind>(parent.value(QStringLiteral("kind")).toInt()));
}

void FolderNavigationController::navigateTo(quint64 handle, bool isRoot)
{
    // Every QML caller (the tree, the pins, the breadcrumb) names a Cloud Drive
    // location: the breadcrumb only makes CloudDrive segments clickable.
    navigateToKind(handle, isRoot, ViewKind::CloudDrive);
}

void FolderNavigationController::navigateToKind(quint64 handle,
                                                bool isRoot,
                                                ViewKind kind,
                                                const QString& revealName)
{
    dropSearchForNavigation();
    mService->navigateTo(static_cast<std::uint64_t>(handle),
                         isRoot,
                         kind,
                         mSortOrder,
                         [this, self = shared_from_this(), revealName](
                             Result<std::vector<FileEntry>> result) {
                             invokeOnGuiThread(this,
                                               [this, result = std::move(result),
                                                revealName]() mutable {
                                                   applyResult(std::move(result), revealName);
                                               });
                         });
}

void FolderNavigationController::goToContainingFolder(quint64 handle, QString name)
{
    mService->resolvePathOf(
        static_cast<std::uint64_t>(handle),
        [this, self = shared_from_this(), name](Result<std::vector<PathSegment>> result) {
            invokeOnGuiThread(this, [this, result = std::move(result), name]() mutable {
                if (!result.success)
                {
                    qCWarning(lcNavigation) << "containing folder lookup failed:"
                                            << QString::fromStdString(result.errorMessage)
                                            << "code=" << result.errorCode;
                    mNotifications->notifyError(QStringLiteral("navigation"),
                                                result.errorCode,
                                                QString::fromStdString(result.errorMessage));
                    return;
                }
                // Root-first and ending with the node itself, so the parent is the
                // one before the end. A chain of one is the node being a root, which
                // no listing can show.
                const std::vector<PathSegment>& segments = result.value();
                if (segments.size() < 2)
                    return;
                const PathSegment& parent = segments[segments.size() - 2];
                navigateToKind(parent.handle, parent.isRoot, parent.kind, name);
            });
        });
}

void FolderNavigationController::applyResult(Result<std::vector<FileEntry>> result,
                                             const QString& revealName)
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
    mListingFromSearch = false;
    mFileListModel->setEntries(std::move(result.value()));
    // Directly, not just via the refreshBreadcrumb -> publishViewKind path below:
    // that one runs only when the resolved path *changed*, and going to the folder a
    // search hit already lives in leaves the breadcrumb exactly as it was.
    publishCrossFolderListing();
    if (!revealName.isEmpty())
    {
        // Only against the listing this very request produced -- see
        // refreshVisibleListing's declaration for why it isn't kept as state.
        const int row = mFileListModel->rowForName(revealName);
        if (row >= 0)
        {
            mFileListModel->selectRow(row, Qt::NoModifier);
            emit revealRowRequested(row);
        }
    }
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
    publishCrossFolderListing();
}

void FolderNavigationController::publishCrossFolderListing()
{
    // Keyed off what the model actually holds, not off searchActive(): a query stays
    // latched after the user opens a folder from its results (a documented property
    // of search() -- navigation state is left alone), so asking the box would keep
    // claiming "these rows come from elsewhere" about a plain folder listing.
    mFileListModel->setCrossFolderListing(mListingFromSearch ||
                                          static_cast<ViewKind>(viewKind()) ==
                                              ViewKind::Favourites);
}

bool FolderNavigationController::canPerform(const QString& actionId) const
{
    const std::string id = actionId.toStdString();
    const ViewKind kind = static_cast<ViewKind>(viewKind());

    // A shortcut stands in for a row of one of the two menus a file view can open --
    // the selection's and the background's -- so either offering it is enough.
    const MenuContext selectionContext{kind,
                                       MenuSite::FileSelection,
                                       mFileListModel->selectionSummary(),
                                       searchActive() || kind == ViewKind::Favourites};
    return menuActionAllowed(id, selectionContext) ||
           menuActionAllowed(id, folderTargetContext(MenuSite::FolderBackground, kind));
}

void FolderNavigationController::dropSearchForNavigation()
{
    if (!searchActive())
        return;
    // Not mListingFromSearch: the results are still on screen until the navigation
    // lands, and applyResult() clears that flag when it does. Clearing it here would
    // claim the rows are one folder's children while they are still hits.
    mLastSearchQuery.clear();
    mSearchFilter = SearchFilter{};
    emit searchActiveChanged();
    emit searchCleared();
}

void FolderNavigationController::search(QString query)
{
    const bool wasActive = searchActive();
    mLastSearchQuery = query.toStdString();
    applySearchCriteria(wasActive);
}

void FolderNavigationController::setSearchFilter(int nodeType,
                                                 int category,
                                                 int createdWithin,
                                                 bool favouritesOnly)
{
    const bool wasActive = searchActive();
    mSearchFilter =
        SearchFilter{clampEnum<SearchNodeType>(nodeType, SearchNodeType::Folders),
                     clampEnum<SearchCategory>(category, SearchCategory::Other),
                     clampEnum<SearchTimeWindow>(createdWithin, SearchTimeWindow::PastYear),
                     favouritesOnly};
    applySearchCriteria(wasActive);
}

void FolderNavigationController::applySearchCriteria(bool wasActive)
{
    if (wasActive != searchActive())
        emit searchActiveChanged();

    if (!searchActive())
    {
        mListingFromSearch = false;
        mFileListModel->setEntries(mLastFolderEntries);
        publishCrossFolderListing();
        return;
    }

    runVisibleSearch();
}

void FolderNavigationController::runVisibleSearch()
{
    auto onSearched = [this, self = shared_from_this()](Result<std::vector<FileEntry>> result) {
        invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
            applySearchResult(std::move(result));
        });
    };

    if (viewKind() == ViewKindEnum::Favourites)
        mService->listFavourites(mSortOrder, mLastSearchQuery, std::move(onSearched));
    else
        mSearchService->search(mLastSearchQuery, mSearchFilter, mSortOrder, std::move(onSearched));
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
    mListingFromSearch = true;
    mFileListModel->setEntries(std::move(result.value()));
    // No breadcrumb change to ride in on, unlike applyResult: searching does not move
    // the tab, so this is the only place the flag can be published from.
    publishCrossFolderListing();
}

void FolderNavigationController::refreshCurrentFolder(QString revealName)
{
    mService->refreshCurrent(
        mSortOrder,
        [this, self = shared_from_this(), revealName](Result<std::vector<FileEntry>> result) {
            invokeOnGuiThread(this, [this, revealName, result = std::move(result)]() mutable {
                applyResult(std::move(result), revealName);
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

void FolderNavigationController::refreshVisibleListing(QString revealName)
{
    if (searchActive())
    {
        // revealName is dropped here on purpose: the visible model is a search
        // result, where "the row named X" is not the thing the caller meant.
        // Re-run the visible search, and separately refresh the cached folder
        // listing (not the visible model) so that clearing the search
        // afterwards doesn't show stale contents/ordering.
        runVisibleSearch();
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

    refreshCurrentFolder(std::move(revealName));
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

const std::vector<FileEntry>& FolderNavigationController::cachedChildren() const
{
    return mLastFolderEntries;
}

bool FolderNavigationController::hasChildFolderNamed(const std::string& name) const
{
    return std::any_of(mLastFolderEntries.begin(),
                       mLastFolderEntries.end(),
                       [&name](const FileEntry& entry) {
                           return entry.isFolder && entry.name == name;
                       });
}

void FolderNavigationController::applyFavouriteChange(quint64 handle, bool favourite)
{
    if (viewKind() == ViewKindEnum::Favourites)
    {
        // Un-favouriting drops the row, and every row after it shifts, so the
        // scroll position moves whatever we do -- which is what makes a re-fetch
        // cheaper than a partial-removal path in FileListModel (spec 4.4).
        refreshVisibleListing();
        return;
    }

    writeFavouriteFlag(handle, favourite);
}

void FolderNavigationController::applyRemoteFavouriteChange(quint64 handle, bool favourite)
{
    if (viewKind() == ViewKindEnum::Favourites)
    {
        markStale();
        return;
    }

    // No membership check: FileListModel::setFavourite ignores a handle it
    // doesn't hold, which is the common case when the toggle happened elsewhere.
    writeFavouriteFlag(handle, favourite);
}

void FolderNavigationController::writeFavouriteFlag(quint64 handle, bool favourite)
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

void FolderNavigationController::markStale()
{
    if (mHasLoadedOnce)
        mStale = true;
}

void FolderNavigationController::refreshIfStale()
{
    if (!mStale)
        return;
    mStale = false;
    refreshVisibleListing();
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
    // A favourites listing shows no folder, so it matches no (handle, isRoot) --
    // yet being a query over the whole drive, any move or copy can change it.
    // Marked rather than re-read: this tab may be in the background, and one
    // full-drive search per moved node is the refresh storm 5.3 warns about.
    if (viewKind() == ViewKindEnum::Favourites)
    {
        markStale();
        return;
    }

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
    const bool wasSearching = searchActive();
    mLastSearchQuery.clear();
    mSearchFilter = SearchFilter{};
    mListingFromSearch = false;
    mHasLoadedOnce = false;
    mStale = false;
    mBreadcrumb.clear();
    publishViewKind();
    mBusy->abandonAll();
    emit canGoBackChanged();
    emit breadcrumbChanged();
    if (wasSearching)
        emit searchActiveChanged();
}
