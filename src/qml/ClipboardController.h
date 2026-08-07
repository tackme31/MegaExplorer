#pragma once
#include "core/NodeRef.h"

#include <QObject>
#include <QVariantList>

#include <vector>

// The app-global copy/cut clipboard: which nodes are on it, cut or copied, and
// which folder they came from. Deliberately holds nothing else and performs no
// operation -- pasting is FileMutationController::paste(), where the
// fan-out/refresh/busy machinery already lives.
//
// App-global because cutting in one tab and pasting in another is the whole point.
// That is also why the source folder is recorded here: by paste time the source tab
// may have navigated away or closed, and the move still has to announce the right
// folder to refresh.
//
// The OS clipboard is deliberately untouched: Ctrl+C/X/V here move MEGA node
// handles, they exchange nothing with Explorer.
class ClipboardController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasContent READ hasContent NOTIFY contentChanged)
    Q_PROPERTY(bool isCut READ isCut NOTIFY contentChanged)
    Q_PROPERTY(int count READ count NOTIFY contentChanged)
    // Handles of the cut nodes, empty in copy mode. A property rather than an
    // isCut(handle) method: a method call reads no property, so a delegate binding on
    // one would never re-evaluate.
    Q_PROPERTY(QVariantList cutHandles READ cutHandles NOTIFY contentChanged)

public:
    explicit ClipboardController(QObject* parent = nullptr);

    // entries are FileListModel::selectedEntries()' maps. The name is kept because a
    // paste-copy needs it to build a non-colliding one; a rename between copy and
    // paste would use the older name, which is harmless and cheaper than re-resolving
    // every handle.
    Q_INVOKABLE void copy(const QVariantList& entries, quint64 sourceHandle, bool sourceIsRoot);
    Q_INVOKABLE void cut(const QVariantList& entries, quint64 sourceHandle, bool sourceIsRoot);
    Q_INVOKABLE void clear();

    // False for a cut going back into the folder it came from -- checkMove refuses
    // that with kEArgs, so letting it through would report N failures for a gesture
    // that means nothing. A *copy* back into its own folder is allowed: that is the
    // auto-rename case.
    Q_INVOKABLE bool canPasteInto(quint64 handle, bool isRoot) const;

    bool hasContent() const;
    bool isCut() const;
    int count() const;
    QVariantList cutHandles() const;

    // Static because the clipboard is not the only source of these maps: a drag
    // carries the same payload and a Ctrl+drop copies from it without touching the
    // clipboard. Here rather than beside NodeRef because this is the QVariant
    // boundary, which src/core stays free of.
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
