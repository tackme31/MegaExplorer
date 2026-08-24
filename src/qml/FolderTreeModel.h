#pragma once
#include "core/FolderTreeService.h"

#include <QAbstractItemModel>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class NotificationController;

// Backs the folder-tree panel: an app-lifetime singleton shared by every tab, owned
// by the composition root and exposed as a context property. No
// enable_shared_from_this, unlike the per-tab controllers -- nothing here can be
// destroyed mid-fetch, so a bare `this` capture is safe.
//
// Lazily expands, folders only, always name-ascending regardless of any tab's sort
// order. Nodes go away only when something asks for it: reset()/reload() through a
// full model reset, refreshFolder() for one subtree.
class FolderTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Roles
    {
        NameRole = Qt::UserRole + 1,
        HandleRole,
        IsRootRole,
    };

    explicit FolderTreeModel(std::shared_ptr<FolderTreeService> service,
                             NotificationController* notifications,
                             QObject* parent = nullptr);
    ~FolderTreeModel() override;

    QModelIndex
    index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool hasChildren(const QModelIndex& parent = QModelIndex()) const override;
    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;

    // Not Q_INVOKABLE: TreeView's expand path reaches it through
    // canFetchMore()/fetchMore(), which is enough on its own. notifyOnFailure is
    // reserved for a refresh the user asked for -- an expand that fails already
    // shows itself, by not opening.
    void ensureLoaded(const QModelIndex& index, bool notifyOnFailure = false);

    // Right-click "Refresh" on a tree row: drops everything below that folder and
    // re-reads its subfolders, leaving the row itself in place (and so expanded)
    // whether or not the folder still exists.
    Q_INVOKABLE void refreshFolder(qulonglong handle, bool isRoot);

    // Login: back to a single "Cloud Drive" node, then load its children.
    Q_INVOKABLE void reload();

    // Logout: the same, without loading, so a later login never briefly shows the
    // previous account's folders.
    Q_INVOKABLE void reset();

private:
    enum class LoadState
    {
        NotLoaded,
        Loading,
        Loaded
    };

    // unique_ptr elements, not values: QModelIndex::internalPointer holds a raw
    // TreeNode*, so a sibling insertion must never move handed-out addresses.
    struct TreeNode
    {
        std::string name;
        std::uint64_t handle = 0;
        bool isRoot = false;
        TreeNode* parent = nullptr;
        std::vector<std::unique_ptr<TreeNode>> children;
        LoadState state = LoadState::NotLoaded;
        // Identifies the one in-flight load this node is waiting for; see
        // mNextLoadToken.
        std::uint64_t loadToken = 0;
    };

    TreeNode* nodeForIndex(const QModelIndex& index) const;
    // By identity rather than address, because that is all a load completion may
    // still hold -- see mNextLoadToken.
    TreeNode* findNode(std::uint64_t handle, bool isRoot) const;
    QModelIndex indexForNode(TreeNode* node) const;
    void resetTree();

    std::shared_ptr<FolderTreeService> mService;
    NotificationController* mNotifications;
    std::unique_ptr<TreeNode> mInvisibleRoot;
    // Never reused, so a completion whose token no longer matches the node it names
    // is stale and drops its result. Covers both ways a load can be outrun: a reset
    // or a refreshFolder() destroying the node (the address a callback captured
    // would dangle, which is why completions re-resolve by handle instead), and a
    // second load starting on the same node before the first came back.
    std::uint64_t mNextLoadToken = 0;
};
