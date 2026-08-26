#pragma once
#include "core/NodeDetailsService.h"

#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>

// Backs the information dialog ("Properties" in Explorer's terms): the row the user
// right-clicked supplies name/size/type immediately, and the two slower answers --
// where the node lives, and what a folder contains -- arrive from
// NodeDetailsService.
//
// One instance for the whole app, like PreviewController: one dialog is open at a
// time, so there is nothing per-tab to keep.
class PropertiesController : public QObject
{
    Q_OBJECT
    // One signal for the whole set: every property changes together, at show() and
    // again when the reply lands, and the dialog binds all of them.
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(bool isFolder READ isFolder NOTIFY changed)
    // The row's own byte count for a file; for a folder, the recursive total, which
    // is 0 until the reply lands.
    Q_PROPERTY(qulonglong sizeBytes READ sizeBytes NOTIFY changed)
    // sizeBytes through QLocale::formattedDataSize, the same call FileListModel's
    // size column makes -- so the dialog and the row can never word one number two
    // ways.
    Q_PROPERTY(QString formattedSize READ formattedSize NOTIFY changed)
    // Unix seconds, as FileListModel's modificationTime role reports them. 0 means
    // the row carried none.
    Q_PROPERTY(qlonglong modificationTime READ modificationTime NOTIFY changed)
    // Folder chain the node sits in, root excluded and joined with '/' -- QML names
    // the root itself from rootKind. Empty means "directly under the root".
    Q_PROPERTY(QString parentPath READ parentPath NOTIFY changed)
    Q_PROPERTY(int rootKind READ rootKind NOTIFY changed)
    // Whether the inspected node is a root itself, which has no location to name.
    Q_PROPERTY(bool isRoot READ isRoot NOTIFY changed)
    // -1 until a folder's contents are known, and permanently for a file.
    Q_PROPERTY(int fileCount READ fileCount NOTIFY changed)
    Q_PROPERTY(int folderCount READ folderCount NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    // Whether the lookup came back failed. The dialog stays open and says so rather
    // than raising a toast: the question was asked in a window that is still there.
    Q_PROPERTY(bool failed READ failed NOTIFY changed)

public:
    explicit PropertiesController(std::shared_ptr<NodeDetailsService> service,
                                  QObject* parent = nullptr);

    QString name() const;
    bool isFolder() const;
    qulonglong sizeBytes() const;
    QString formattedSize() const;
    qlonglong modificationTime() const;
    QString parentPath() const;
    int rootKind() const;
    bool isRoot() const;
    int fileCount() const;
    int folderCount() const;
    bool loading() const;
    bool failed() const;

    // Publishes what the row already knows, starts the lookup, and asks the dialog
    // to open. Everything but the handle comes from the caller because the listing
    // has it and re-reading it would cost a round-trip. isRoot names the case a row
    // can't be: a root's handle is 0, so the flag is the only thing pointing at it.
    Q_INVOKABLE void show(quint64 handle,
                          bool isRoot,
                          const QString& name,
                          bool isFolder,
                          qulonglong sizeBytes,
                          qlonglong modificationTime);

signals:
    void changed();
    // Main.qml's dialog opens on this rather than on a `visible` property here: the
    // dialog owns its own open/closed state, and re-asking for the same node has to
    // re-open a dialog the user closed.
    void showRequested();

private:
    std::shared_ptr<NodeDetailsService> mService;

    // Which show() the next reply belongs to. Without it a slow lookup for the
    // previously inspected node overwrites the one the dialog is now showing.
    std::uint64_t mRequestId = 0;

    QString mName;
    bool mIsFolder = false;
    qulonglong mSizeBytes = 0;
    qlonglong mModificationTime = 0;
    QString mParentPath;
    int mRootKind = 0;
    bool mIsRoot = false;
    int mFileCount = -1;
    int mFolderCount = -1;
    bool mLoading = false;
    bool mFailed = false;
};
