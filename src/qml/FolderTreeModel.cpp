#include "FolderTreeModel.h"

#include "app/Logging.h"
#include "GuiThread.h"
#include "NotificationController.h"

#include <utility>

FolderTreeModel::FolderTreeModel(std::shared_ptr<FolderTreeService> service,
                                 NotificationController* notifications,
                                 QObject* parent)
    : QAbstractItemModel(parent), mService(std::move(service)), mNotifications(notifications)
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
    // Ask the SDK's in-memory tree rather than assuming yes: assuming puts an expand
    // arrow on every childless folder with no later correction, since TreeView's
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

void FolderTreeModel::ensureLoaded(const QModelIndex& index, bool notifyOnFailure)
{
    TreeNode* node = nodeForIndex(index);
    if (!node || node->state != LoadState::NotLoaded)
        return;
    node->state = LoadState::Loading;

    const std::uint64_t token = ++mNextLoadToken;
    node->loadToken = token;
    const std::uint64_t handle = node->handle;
    const bool isRoot = node->isRoot;

    mService->loadSubfolders(
        handle,
        isRoot,
        [this, handle, isRoot, token, notifyOnFailure](Result<std::vector<FileEntry>> result) {
            invokeOnGuiThread(this,
                              [this, handle, isRoot, token, notifyOnFailure,
                               result = std::move(result)]() mutable {
                                  // Re-resolved rather than captured: a sign-out, a
                                  // sign-in's reload() or a refreshFolder() above this
                                  // node destroys it while the fetch is in flight, so
                                  // a TreeNode* would dangle here.
                                  TreeNode* node = findNode(handle, isRoot);
                                  if (!node || node->loadToken != token)
                                      return;

                                  if (!result.success)
                                  {
                                      qCWarning(lcNavigation)
                                          << "folder tree load failed:"
                                          << QString::fromStdString(result.errorMessage)
                                          << "code=" << result.errorCode;
                                      // Reset to NotLoaded rather than leaving it stuck
                                      // in Loading, so re-expanding retries instead of
                                      // silently never fetching again.
                                      node->state = LoadState::NotLoaded;
                                      // Inserting nothing emits nothing, so without
                                      // this a failed refresh leaves the row showing
                                      // the "no children" answer the removal left
                                      // behind -- and an unexpandable row cannot
                                      // retry through fetchMore().
                                      const QModelIndex idx = indexForNode(node);
                                      emit dataChanged(idx, idx);
                                      if (notifyOnFailure && mNotifications)
                                      {
                                          mNotifications->notifyError(
                                              QStringLiteral("refresh"),
                                              result.errorCode,
                                              QString::fromStdString(result.errorMessage));
                                      }
                                      return;
                                  }

                                  if (!result.value().empty())
                                  {
                                      const QModelIndex parentIndex = indexForNode(node);
                                      beginInsertRows(parentIndex,
                                                      0,
                                                      static_cast<int>(result.value().size()) - 1);
                                      node->children.reserve(result.value().size());
                                      for (FileEntry& entry : result.value())
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
                                      // beginInsertRows with an empty range is invalid,
                                      // so just flip the state and notify. Nothing
                                      // depends on the view acting on that: a childless
                                      // folder never grew an expand arrow to retract.
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

void FolderTreeModel::refreshFolder(qulonglong handle, bool isRoot)
{
    const std::uint64_t nodeHandle = handle;
    TreeNode* node = findNode(nodeHandle, isRoot);
    if (!node)
        return;

    // Claims the node the way a load does, because the continuation below cannot
    // tell "the node I started on" from "a sign-out rebuilt the tree and this
    // handle names a different one" by identity alone -- resetTree() gives the new
    // root the same handle 0 / isRoot pair. Also drops any expand already in
    // flight on this node, which this refresh supersedes.
    const std::uint64_t token = ++mNextLoadToken;
    node->loadToken = token;

    // Catch up first: without it the re-read below would hand back the same cached
    // children, since the SDK's getters only report what it was last told.
    mService->syncWithServer([this, nodeHandle, isRoot, token](Result<void> result) {
        invokeOnGuiThread(this, [this, nodeHandle, isRoot, token, result = std::move(result)]() {
            if (!result.success)
            {
                // Logged, not toasted: the re-read below reports its own failure, and
                // a stale-but-readable tree is still worth showing.
                qCWarning(lcNavigation)
                    << "folder tree sync failed:" << QString::fromStdString(result.errorMessage)
                    << "code=" << result.errorCode;
            }

            TreeNode* node = findNode(nodeHandle, isRoot);
            if (!node || node->loadToken != token)
                return;

            const QModelIndex index = indexForNode(node);
            // Before the removal, not after: TreeView's proxy re-reads hasChildren()
            // from inside endRemoveRows() and nowhere else, and with the children
            // gone but the state still Loaded that call answers "no" -- retracting an
            // arrow the reload is about to need. rowCount() ignores state, so moving
            // the flip here changes nothing the removal itself reports.
            node->state = LoadState::NotLoaded;
            // Dropping the whole subtree is what collapses the descendants: the view
            // tracks expansion per index, and every one of those just went away.
            if (!node->children.empty())
            {
                beginRemoveRows(index, 0, static_cast<int>(node->children.size()) - 1);
                node->children.clear();
                endRemoveRows();
            }
            ensureLoaded(index, true);
        });
    });
}

FolderTreeModel::TreeNode* FolderTreeModel::nodeForIndex(const QModelIndex& index) const
{
    if (!index.isValid())
        return mInvisibleRoot.get();
    return static_cast<TreeNode*>(index.internalPointer());
}

FolderTreeModel::TreeNode* FolderTreeModel::findNode(std::uint64_t handle, bool isRoot) const
{
    // Linear, but only over what the user has expanded -- MEGA gives a folder one
    // parent, so the first match is the only one.
    std::vector<TreeNode*> pending{mInvisibleRoot.get()};
    while (!pending.empty())
    {
        TreeNode* node = pending.back();
        pending.pop_back();
        if (node != mInvisibleRoot.get() && node->handle == handle && node->isRoot == isRoot)
            return node;
        for (const std::unique_ptr<TreeNode>& child: node->children)
            pending.push_back(child.get());
    }
    return nullptr;
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

    mInvisibleRoot = std::make_unique<TreeNode>();
    // Fixed single child, set once and never re-fetched.
    mInvisibleRoot->state = LoadState::Loaded;

    auto cloudDrive = std::make_unique<TreeNode>();
    // Hardcoded rather than composed in QML, unlike the other "Cloud Drive" labels:
    // the panel uses TreeViewDelegate's default contentItem, which reads
    // Qt::DisplayRole directly with no isRoot-aware composition step.
    cloudDrive->name = "Cloud Drive";
    cloudDrive->isRoot = true;
    cloudDrive->parent = mInvisibleRoot.get();
    mInvisibleRoot->children.push_back(std::move(cloudDrive));

    endResetModel();
}
