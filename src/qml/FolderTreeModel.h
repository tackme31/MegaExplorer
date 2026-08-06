#pragma once
#include "core/FolderTreeService.h"

#include <QAbstractItemModel>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Backs FolderTreePanel.qml's TreeView: an app-lifetime singleton shared by
// every tab (docs/PROGRESS.md's Phase 10 entry -- "shared across tabs, not
// duplicated per tab"), owned by main.cpp's composition root as a stack
// local (like NotificationController) and exposed via setContextProperty, no
// QML_ELEMENT needed. Unlike FolderNavigationController/ThumbnailController
// (Phase 9), this class does NOT use enable_shared_from_this: those two
// needed it because a tab (and its controllers) can be destroyed mid-fetch
// by closing it, but this model has no such per-tab lifetime -- it lives as
// long as the app does, so a bare `this` capture in async callbacks is safe.
//
// Lazily expands via FolderTreeService::loadSubfolders (folders only, always
// name-ascending -- independent of whatever sort order any tab's file list
// is currently using). Nodes are never removed once loaded: the tree only
// grows for the lifetime of a login session, reset() (logout) or reload()
// (fresh login) are the only ways it shrinks, both via a full model reset.
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

    // Starts the fetch for index's children if it hasn't been started yet.
    // Not Q_INVOKABLE: QML never calls this -- TreeView's expand path reaches
    // it through canFetchMore()/fetchMore() above, which is enough on its own
    // (verified against Qt 6.11's TreeView proxy).
    void ensureLoaded(const QModelIndex& index);

    // Resets the tree to a single, unloaded "Cloud Drive" node and
    // immediately starts loading its children -- called on login (including
    // a fresh account after a previous logout).
    Q_INVOKABLE void reload();

    // Resets the tree to a single, unloaded "Cloud Drive" node without
    // loading anything -- called on logout, so a subsequent login never
    // briefly shows the previous account's folders.
    Q_INVOKABLE void reset();

private:
    enum class LoadState
    {
        NotLoaded,
        Loading,
        Loaded
    };

    // children is a vector<unique_ptr<TreeNode>>, not vector<TreeNode>: a
    // QModelIndex::internalPointer stores a raw TreeNode*, so a sibling
    // insertion reallocating the vector must never move already-handed-out
    // node addresses.
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
    // Bumped by every resetTree(). An in-flight load captures the value it
    // started under and drops its result if it no longer matches: reset()
    // (sign-out) and reload() (sign-in) destroy the whole node tree, so the
    // raw TreeNode* an in-flight callback captured would otherwise dangle.
    std::uint64_t mGeneration = 0;
};
