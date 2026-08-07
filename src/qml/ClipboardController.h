#pragma once
#include "core/NodeRef.h"

#include <QObject>
#include <QVariantList>

#include <vector>

// The app-global copy/cut clipboard: which nodes are on it, whether they were
// cut or copied, and which folder they came from. Deliberately holds nothing
// else -- it has no services, no SDK, and performs no operation; pasting is
// FolderNavigationController::paste(), because the destination is always a
// tab's current folder and the fan-out/refresh/busy machinery already lives
// there.
//
// App-global rather than per-tab for the obvious reason: cutting in one tab and
// pasting in another is the whole point. It is also why the source folder is
// recorded here -- by the time the paste happens, the tab the nodes came from
// may have navigated elsewhere or been closed, and the move still has to
// announce the right folder to refresh.
//
// The OS clipboard is deliberately untouched (Phase 23): Ctrl+C/X/V here move
// MEGA node handles around, they don't exchange anything with Explorer.
//
// Registered as the "clipboardController" context property (main.cpp) and
// injected into every FolderNavigationController as a non-owning pointer, same
// arrangement as NotificationController.
class ClipboardController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasContent READ hasContent NOTIFY contentChanged)
    Q_PROPERTY(bool isCut READ isCut NOTIFY contentChanged)
    Q_PROPERTY(int count READ count NOTIFY contentChanged)
    // Handles of the cut nodes, empty in copy mode -- what both file views bind
    // their ghosted-delegate opacity to. A property rather than an
    // isCut(handle) method because a method call reads no property, so a
    // delegate binding on one would never re-evaluate when the clipboard
    // changes.
    Q_PROPERTY(QVariantList cutHandles READ cutHandles NOTIFY contentChanged)

public:
    explicit ClipboardController(QObject* parent = nullptr);

    // entries are FileListModel::selectedEntries()' maps, passed straight
    // through. The name is kept because a paste-copy needs it to build a
    // non-colliding one (FileOperationService::uniqueCopyName) -- a rename
    // between copying and pasting would use the older name, which is harmless
    // and cheaper than resolving every handle again at paste time.
    Q_INVOKABLE void copy(const QVariantList& entries, quint64 sourceHandle, bool sourceIsRoot);
    Q_INVOKABLE void cut(const QVariantList& entries, quint64 sourceHandle, bool sourceIsRoot);
    Q_INVOKABLE void clear();

    // Whether pasting into (handle, isRoot) could do anything: something is on
    // the clipboard, and it isn't a cut going back into the folder it came
    // from -- which IMegaClient::checkMove refuses with kEArgs, so letting it
    // through would report N failures for a gesture that means nothing. A
    // *copy* back into its own folder is allowed: that is the auto-rename case.
    Q_INVOKABLE bool canPasteInto(quint64 handle, bool isRoot) const;

    bool hasContent() const;
    bool isCut() const;
    int count() const;
    QVariantList cutHandles() const;

    // The QVariantMap -> NodeRef conversion, static because the clipboard is
    // not the only source of these maps any more: a drag carries the same
    // FileListModel::selectedEntries() payload, and a Ctrl+drop copies from it
    // without ever touching the clipboard. Lives here rather than beside
    // NodeRef because it is the QVariant boundary, which src/core stays free of.
    static std::vector<NodeRef> toNodeRefs(const QVariantList& entries);

    // Typed accessors for FolderNavigationController; not QML-facing.
    const std::vector<NodeRef>& entries() const;
    quint64 sourceHandle() const;
    bool sourceIsRoot() const;

signals:
    void contentChanged();

private:
    void take(const QVariantList& entries, bool cut, quint64 sourceHandle, bool sourceIsRoot);

    std::vector<NodeRef> mEntries;
    bool mIsCut = false;
    quint64 mSourceHandle = 0;
    bool mSourceIsRoot = true;
};
