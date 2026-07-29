#include "TabsController.h"

#include "FolderNavigationController.h"
#include "ThumbnailController.h"

#include <QQmlEngine>

#include <algorithm>

TabsController::TabsController(std::function<TabContext()> factory, QObject* parent)
    : QAbstractListModel(parent), mFactory(std::move(factory))
{
    // One tab up front, before QML ever attaches to this model -- no
    // begin/endInsertRows needed for a mutation nothing has observed yet.
    mTabs.push_back(createTab());
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
        case ThumbnailsRole:
            return QVariant::fromValue(static_cast<QObject*>(
                mTabs[static_cast<std::size_t>(index.row())].thumbnails.get()));
    }
    return QVariant();
}

QHash<int, QByteArray> TabsController::roleNames() const
{
    return {
        {TitleRole, "title"},
        {AtRootRole, "atRoot"},
        {NavigationRole, "navigation"},
        {ThumbnailsRole, "thumbnails"},
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

    setCurrentIndex(row);
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
    QQmlEngine::setObjectOwnership(context.thumbnails.get(), QQmlEngine::CppOwnership);

    // Relays this tab's title-affecting changes into a per-row dataChanged()
    // so TabStrip.qml's TabButton delegates update without needing their own
    // Connections. Looked up by pointer rather than a captured index: tabs
    // can be inserted/removed after this connection is made, which would
    // otherwise leave a stale row number behind.
    FolderNavigationController* navigation = context.navigation.get();
    connect(navigation, &FolderNavigationController::breadcrumbChanged, this, [this, navigation]() {
        for (std::size_t i = 0; i < mTabs.size(); ++i)
        {
            if (mTabs[i].navigation.get() == navigation)
            {
                const QModelIndex idx = index(static_cast<int>(i));
                emit dataChanged(idx, idx, {TitleRole, AtRootRole});
                break;
            }
        }
    });

    return context;
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
