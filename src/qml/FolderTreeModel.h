#pragma once
#include "core/FolderTreeService.h"

#include <QAbstractItemModel>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Backs the folder-tree panel: an app-lifetime singleton shared by every tab, owned
// by the composition root and exposed as a context property. No
// enable_shared_from_this, unlike the per-tab controllers -- nothing here can be
// destroyed mid-fetch, so a bare `this` capture is safe.
//
// Lazily expands, folders only, always name-ascending regardless of any tab's sort
// order. Nodes are never removed once loaded: the tree only grows within a login
// session, and reset()/reload() shrink it through a full model reset.
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

    explicit FolderTreeModel(std::shared_ptr<FolderTreeService> service, QObject* parent = nullptr);
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
    // canFetchMore()/fetchMore(), which is enough on its own.
    void ensureLoaded(const QModelIndex& index);

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
    };

    TreeNode* nodeForIndex(const QModelIndex& index) const;
    QModelIndex indexForNode(TreeNode* node) const;
    void resetTree();

    std::shared_ptr<FolderTreeService> mService;
    std::unique_ptr<TreeNode> mInvisibleRoot;
    // Bumped by every resetTree(); an in-flight load drops its result if the value
    // changed. Both resets destroy the whole node tree, so the raw TreeNode* a
    // callback captured would otherwise dangle.
    std::uint64_t mGeneration = 0;
};
