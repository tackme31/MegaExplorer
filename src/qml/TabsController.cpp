#include "TabsController.h"

#include "FileMutationController.h"
#include "FolderNavigationController.h"
#include "ThumbnailController.h"
#include "UploadController.h"

#include <QQmlEngine>

#include <algorithm>

TabsController::TabsController(std::function<TabContext()> factory,
                               UploadController* uploads,
                               QObject* parent)
    : QAbstractListModel(parent), mFactory(std::move(factory)), mUploads(uploads)
{
    // One tab up front, before QML ever attaches to this model -- no
    // begin/endInsertRows needed for a mutation nothing has observed yet.
    mTabs.push_back(createTab());

    // Which tabs an upload makes busy depends on where each one is standing,
    // so any change to the destination set is a change to every row.
    connect(mUploads, &UploadController::activeDestinationsChanged, this, [this]() {
        if (mTabs.empty())
            return;
        emit dataChanged(index(0), index(static_cast<int>(mTabs.size()) - 1), {BusyRole});
    });
}

int TabsController::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(mTabs.size());
}

QVariant TabsController::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(mTabs.size()))
        return QVariant();

    const FolderNavigationController* navigation =
        mTabs[static_cast<std::size_t>(index.row())].navigation.get();
    switch (role)
    {
        case TitleRole:
            return navigation->currentFolderName();
        case AtRootRole:
            return navigation->atRoot();
        case NavigationRole:
            return QVariant::fromValue(static_cast<QObject*>(
                mTabs[static_cast<std::size_t>(index.row())].navigation.get()));
        case MutationsRole:
            return QVariant::fromValue(static_cast<QObject*>(
                mTabs[static_cast<std::size_t>(index.row())].mutations.get()));
        case ThumbnailsRole:
            return QVariant::fromValue(static_cast<QObject*>(
                mTabs[static_cast<std::size_t>(index.row())].thumbnails.get()));
        case BusyRole:
            // An upload has no owning tab, so it can't drive the per-tab
            // controller's own busy the way that tab's mutations do -- fold it
            // in here instead, for whichever tabs are showing the destination.
            return navigation->busy() ||
                   mUploads->isUploadingTo(navigation->currentHandle(), navigation->atRoot());
    }
    return QVariant();
}

QHash<int, QByteArray> TabsController::roleNames() const
{
    return {
        {TitleRole, "title"},
        {AtRootRole, "atRoot"},
        {NavigationRole, "navigation"},
        {MutationsRole, "mutations"},
        {ThumbnailsRole, "thumbnails"},
        {BusyRole, "busy"},
    };
}

int TabsController::currentIndex() const
{
    return mCurrentIndex;
}

void TabsController::setCurrentIndex(int index)
{
    if (mTabs.empty())
        return;
    index = std::max(0, std::min(index, static_cast<int>(mTabs.size()) - 1));
    if (index == mCurrentIndex)
        return;
    mCurrentIndex = index;
    emit currentTabChanged();
}

QObject* TabsController::currentNavigation() const
{
    if (mCurrentIndex < 0 || mCurrentIndex >= static_cast<int>(mTabs.size()))
        return nullptr;
    return mTabs[static_cast<std::size_t>(mCurrentIndex)].navigation.get();
}

int TabsController::count() const
{
    return static_cast<int>(mTabs.size());
}

void TabsController::addTab()
{
    const int row = static_cast<int>(mTabs.size());
    beginInsertRows(QModelIndex(), row, row);
    mTabs.push_back(createTab());
    endInsertRows();
    emit countChanged();
    mTabs.back().navigation->loadRoot();
    setCurrentIndex(row);
}

void TabsController::addTabAt(quint64 handle, bool isRoot)
{
    const int row = static_cast<int>(mTabs.size());
    beginInsertRows(QModelIndex(), row, row);
    mTabs.push_back(createTab());
    endInsertRows();
    emit countChanged();

    FolderNavigationController* navigation = mTabs.back().navigation.get();
    if (isRoot)
        navigation->loadRoot();
    else
        navigation->openFolder(handle);
}

void TabsController::closeTab(int index)
{
    if (index < 0 || index >= static_cast<int>(mTabs.size()))
        return;

    const int oldCurrent = mCurrentIndex;

    beginRemoveRows(QModelIndex(), index, index);
    mTabs.erase(mTabs.begin() + index);
    endRemoveRows();
    emit countChanged();

    if (mTabs.empty())
    {
        mCurrentIndex = -1;
        emit currentTabChanged();
        emit lastTabClosed();
        return;
    }

    // Closing a tab before the active one shifts the active tab's row down
    // by one; closing the active tab itself lands on whatever slid into its
    // slot, clamped to the new last row if it was the last tab. Closing a
    // tab after the active one changes neither the active row's index nor
    // its identity, so currentIndex/currentTabChanged are left untouched.
    if (index < oldCurrent)
    {
        mCurrentIndex = oldCurrent - 1;
        emit currentTabChanged();
    }
    else if (index == oldCurrent)
    {
        mCurrentIndex = std::min(oldCurrent, static_cast<int>(mTabs.size()) - 1);
        emit currentTabChanged();
    }
}

void TabsController::moveTab(int from, int to)
{
    const int size = static_cast<int>(mTabs.size());
    if (from < 0 || from >= size || to < 0 || to >= size || from == to)
        return;

    const auto first = mTabs.begin();
    const auto at = [first](int i) {
        return first + static_cast<std::vector<TabContext>::difference_type>(i);
    };

    // beginMoveRows' destination is an insertion point in the *pre-move*
    // coordinates, so moving right has to name the row after the target --
    // the same off-by-one QuickAccessModel::move documents.
    beginMoveRows(QModelIndex(), from, from, QModelIndex(), to > from ? to + 1 : to);
    if (from < to)
        std::rotate(at(from), at(from + 1), at(to + 1));
    else
        std::rotate(at(to), at(from), at(from + 1));
    endMoveRows();

    // The active tab keeps its identity, not its row number: dragging some
    // other tab across it slides it one slot the other way. Emitted after
    // endMoveRows so Main.qml's StackLayout never sees the new index against
    // the old row order.
    int current = mCurrentIndex;
    if (current == from)
        current = to;
    else if (from < current && current <= to)
        --current;
    else if (to <= current && current < from)
        ++current;

    if (current != mCurrentIndex)
    {
        mCurrentIndex = current;
        emit currentTabChanged();
    }
}

void TabsController::loadRootAll()
{
    collapseToSingleTab();
    mTabs.front().navigation->loadRoot();
}

void TabsController::resetAll()
{
    collapseToSingleTab();
    mTabs.front().navigation->reset();
}

TabContext TabsController::createTab()
{
    TabContext context = mFactory();
    // TabContext's shared_ptr members are unparented (see TabsController.h's
    // struct comment), so QML would otherwise default them to
    // JavaScriptOwnership the first time a QObject* role/property hands one
    // across the engine boundary -- its GC could then delete a controller
    // out from under the shared_ptrs still holding it alive.
    QQmlEngine::setObjectOwnership(context.navigation.get(), QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(context.mutations.get(), QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(context.thumbnails.get(), QQmlEngine::CppOwnership);
    // Same reason once more: since R2-4 the FileListModel is an unparented
    // heap QObject too, and it crosses the engine boundary through
    // FolderNavigationController's fileListModel property.
    QQmlEngine::setObjectOwnership(context.navigation->fileListModelForThumbnails().get(),
                                   QQmlEngine::CppOwnership);

    // Relays this tab's own changes into a per-row dataChanged() so
    // TabStrip.qml's TabButton delegates update without needing their own
    // Connections.
    FolderNavigationController* navigation = context.navigation.get();
    connect(navigation, &FolderNavigationController::breadcrumbChanged, this, [this, navigation]() {
        // BusyRole too: it's partly a function of where this tab is standing
        // (see data()), so navigating into or out of an upload destination
        // changes it without any operation of this tab's own starting.
        emitRowChangedFor(navigation, {TitleRole, AtRootRole, BusyRole});
    });
    connect(navigation, &FolderNavigationController::busyChanged, this, [this, navigation]() {
        emitRowChangedFor(navigation, {BusyRole});
    });

    // Phase 22b: a drop onto another tab leaves that tab's listing on screen
    // and stale, which Phase 14a could get away with (nothing else was
    // visible) and this can't. The mover has already refreshed itself, so
    // only the other tabs are fanned out to -- both ends of the move, since
    // one folder lost nodes and another gained them. The folder tree still
    // isn't refreshed; that stays Phase 16's, for copies (Phase 23) as much as
    // for moves.
    //
    // Sent by the mutation half, received on behalf of the navigation halves:
    // the two are 1:1 per tab, so the skip-self check below still compares
    // navigation pointers (R5-1).
    connect(context.mutations.get(),
            &FileMutationController::nodesMoved,
            this,
            [this, navigation](
                quint64 destination, bool destinationIsRoot, quint64 source, bool sourceIsRoot) {
                for (const TabContext& tab : mTabs)
                {
                    if (tab.navigation.get() == navigation)
                        continue;
                    tab.navigation->refreshIfShowing(destination, destinationIsRoot);
                    tab.navigation->refreshIfShowing(source, sourceIsRoot);
                }
            });

    // Same fan-out for a paste-copy, but with one end: a copy fills the
    // destination and leaves the folder the nodes came from alone.
    connect(context.mutations.get(),
            &FileMutationController::nodesCopied,
            this,
            [this, navigation](quint64 destination, bool destinationIsRoot) {
                for (const TabContext& tab : mTabs)
                {
                    if (tab.navigation.get() == navigation)
                        continue;
                    tab.navigation->refreshIfShowing(destination, destinationIsRoot);
                }
            });

    return context;
}

void TabsController::emitRowChangedFor(const FolderNavigationController* navigation,
                                       const QList<int>& roles)
{
    for (std::size_t i = 0; i < mTabs.size(); ++i)
    {
        if (mTabs[i].navigation.get() == navigation)
        {
            const QModelIndex idx = index(static_cast<int>(i));
            emit dataChanged(idx, idx, roles);
            return;
        }
    }
}

void TabsController::collapseToSingleTab()
{
    if (mTabs.size() > 1)
    {
        beginRemoveRows(QModelIndex(), 1, static_cast<int>(mTabs.size()) - 1);
        mTabs.erase(mTabs.begin() + 1, mTabs.end());
        endRemoveRows();
        emit countChanged();
    }
    else if (mTabs.empty())
    {
        beginInsertRows(QModelIndex(), 0, 0);
        mTabs.push_back(createTab());
        endInsertRows();
        emit countChanged();
    }

    if (mCurrentIndex != 0)
    {
        mCurrentIndex = 0;
        emit currentTabChanged();
    }
}
