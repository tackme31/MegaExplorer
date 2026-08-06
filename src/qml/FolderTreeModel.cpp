#include "FolderTreeModel.h"

#include "app/Logging.h"
#include "GuiThread.h"

#include <utility>

FolderTreeModel::FolderTreeModel(std::shared_ptr<FolderTreeService> service, QObject* parent)
    : QAbstractItemModel(parent), mService(std::move(service))
{
    resetTree();
}

FolderTreeModel::~FolderTreeModel() = default;

QModelIndex FolderTreeModel::index(int row, int column, const QModelIndex& parent) const
{
    if (column != 0 || row < 0)
        return QModelIndex();

    TreeNode* parentNode = nodeForIndex(parent);
    if (!parentNode || row >= static_cast<int>(parentNode->children.size()))
        return QModelIndex();

    return createIndex(row, column, parentNode->children[static_cast<std::size_t>(row)].get());
}

QModelIndex FolderTreeModel::parent(const QModelIndex& child) const
{
    TreeNode* node = nodeForIndex(child);
    if (!node || node == mInvisibleRoot.get())
        return QModelIndex();
    return indexForNode(node->parent);
}

int FolderTreeModel::rowCount(const QModelIndex& parent) const
{
    if (parent.column() > 0)
        return 0;
    TreeNode* node = nodeForIndex(parent);
    return node ? static_cast<int>(node->children.size()) : 0;
}

int FolderTreeModel::columnCount(const QModelIndex&) const
{
    return 1;
}

QVariant FolderTreeModel::data(const QModelIndex& index, int role) const
{
    TreeNode* node = nodeForIndex(index);
    if (!node || node == mInvisibleRoot.get())
        return {};

    switch (role)
    {
        case Qt::DisplayRole:
        case NameRole:
            return QString::fromStdString(node->name);
        case HandleRole:
            return QVariant(static_cast<qulonglong>(node->handle));
        case IsRootRole:
            return node->isRoot;
        default:
            return {};
    }
}

QHash<int, QByteArray> FolderTreeModel::roleNames() const
{
    return {
        {Qt::DisplayRole, "display"},
        {NameRole, "name"},
        {HandleRole, "handle"},
        {IsRootRole, "isRoot"},
    };
}

bool FolderTreeModel::hasChildren(const QModelIndex& parent) const
{
    if (parent.column() > 0)
        return false;
    TreeNode* node = nodeForIndex(parent);
    if (!node)
        return false;
    if (node->state == LoadState::Loaded)
        return !node->children.empty();
    // Not loaded yet, so ask the SDK's in-memory node tree directly rather
    // than assuming yes: assuming put an expand arrow on every childless
    // folder, and there was no later correction for it -- TreeView's internal
    // proxy isn't guaranteed to re-query hasChildren() after a dataChanged().
    return mService->hasSubfolders(node->handle, node->isRoot);
}

bool FolderTreeModel::canFetchMore(const QModelIndex& parent) const
{
    TreeNode* node = nodeForIndex(parent);
    return node && node->state == LoadState::NotLoaded;
}

void FolderTreeModel::fetchMore(const QModelIndex& parent)
{
    ensureLoaded(parent);
}

void FolderTreeModel::ensureLoaded(const QModelIndex& index)
{
    TreeNode* node = nodeForIndex(index);
    if (!node || node->state != LoadState::NotLoaded)
        return;
    node->state = LoadState::Loading;

    const std::uint64_t generation = mGeneration;
    mService->loadSubfolders(
        node->handle,
        node->isRoot,
        [this, node, generation](Result<std::vector<FileEntry>> result) {
            invokeOnGuiThread(this, [this, node, generation, result = std::move(result)]() mutable {
                // The tree was reset out from under this fetch (sign-out, or
                // a sign-in's reload()) -- node has been destroyed since.
                if (generation != mGeneration)
                    return;

                if (!result.success)
                {
                    qCWarning(lcNavigation)
                        << "folder tree load failed:" << QString::fromStdString(result.errorMessage)
                        << "code=" << result.errorCode;
                    // Reset to NotLoaded rather than leaving it stuck in
                    // Loading, so re-expanding retries instead of silently
                    // never fetching again.
                    node->state = LoadState::NotLoaded;
                    return;
                }

                if (!result.value.empty())
                {
                    const QModelIndex parentIndex = indexForNode(node);
                    beginInsertRows(parentIndex, 0, static_cast<int>(result.value.size()) - 1);
                    node->children.reserve(result.value.size());
                    for (FileEntry& entry : result.value)
                    {
                        auto child = std::make_unique<TreeNode>();
                        child->name = std::move(entry.name);
                        child->handle = entry.handle;
                        child->isRoot = false;
                        child->parent = node;
                        node->children.push_back(std::move(child));
                    }
                    endInsertRows();
                    node->state = LoadState::Loaded;
                }
                else
                {
                    // beginInsertRows with an empty range is invalid, so just
                    // flip the state and notify. Nothing depends on the view
                    // acting on that dataChanged(): hasChildren() consults the
                    // SDK for unloaded nodes, so a childless folder never grew
                    // an expand arrow that would now need retracting.
                    node->state = LoadState::Loaded;
                    const QModelIndex idx = indexForNode(node);
                    emit dataChanged(idx, idx);
                }
            });
        });
}

void FolderTreeModel::reload()
{
    resetTree();
    ensureLoaded(index(0, 0, QModelIndex()));
}

void FolderTreeModel::reset()
{
    resetTree();
}

FolderTreeModel::TreeNode* FolderTreeModel::nodeForIndex(const QModelIndex& index) const
{
    if (!index.isValid())
        return mInvisibleRoot.get();
    return static_cast<TreeNode*>(index.internalPointer());
}

QModelIndex FolderTreeModel::indexForNode(TreeNode* node) const
{
    if (!node || node == mInvisibleRoot.get())
        return QModelIndex();

    TreeNode* parent = node->parent;
    for (std::size_t row = 0; row < parent->children.size(); ++row)
    {
        if (parent->children[row].get() == node)
            return createIndex(static_cast<int>(row), 0, node);
    }
    return QModelIndex();
}

void FolderTreeModel::resetTree()
{
    beginResetModel();

    ++mGeneration;
    mInvisibleRoot = std::make_unique<TreeNode>();
    // Fixed single child, set once here and never re-fetched -- only the
    // "Cloud Drive" node itself (and whatever gets lazily loaded under it)
    // ever needs canFetchMore()/ensureLoaded() to do anything.
    mInvisibleRoot->state = LoadState::Loaded;

    auto cloudDrive = std::make_unique<TreeNode>();
    // Hardcoded rather than composed in QML (unlike Breadcrumb.qml/
    // TabStrip.qml's own "Cloud Drive" labels): FolderTreePanel.qml uses
    // TreeViewDelegate's default contentItem unmodified, which reads
    // Qt::DisplayRole directly with no isRoot-aware composition step.
    cloudDrive->name = "Cloud Drive";
    cloudDrive->isRoot = true;
    cloudDrive->parent = mInvisibleRoot.get();
    mInvisibleRoot->children.push_back(std::move(cloudDrive));

    endResetModel();
}
