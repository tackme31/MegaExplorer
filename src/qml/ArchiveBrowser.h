#pragma once
#include "ArchiveTree.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <optional>
#include <QtQml/qqmlregistration.h>

// One archive viewer window's listing: where in the archive it is, and what that
// folder holds. ViewerController::openArchive fills it; the window only navigates.
//
// One per window rather than a shared controller, since several viewers stand at
// once, each in a folder of its own.
class ArchiveBrowser : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Returned by ViewerController::openArchive")

public:
    enum State
    {
        Loading,
        Ready,
        Failed // reason says why
    };
    Q_ENUM(State)

    enum Reason
    {
        NoReason,
        Unreadable, // no central directory could be located or parsed
        Empty,      // a readable zip that holds no entries
        FetchFailed // a range read failed
    };
    Q_ENUM(Reason)

    // One NOTIFY for all of them, for PreviewController's reason: they move together,
    // and separate signals would let QML see a new path beside the old entries.
    Q_PROPERTY(State state READ state NOTIFY changed)
    Q_PROPERTY(Reason reason READ reason NOTIFY changed)
    // Folder names from the archive root down to the one shown; empty at the root.
    Q_PROPERTY(QStringList path READ path NOTIFY changed)
    // The shown folder's rows, folders first: name, isDirectory, formattedSize.
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)

    explicit ArchiveBrowser(QObject* parent = nullptr);

    void setTree(ArchiveTree tree);
    void fail(Reason reason);

    // A name that is not a folder in the one shown does nothing.
    Q_INVOKABLE void openFolder(const QString& name);
    Q_INVOKABLE void goUp();
    // Keeps the first depth names of path: 0 is the archive root.
    Q_INVOKABLE void goToDepth(int depth);

    State state() const
    {
        return mState;
    }
    Reason reason() const
    {
        return mReason;
    }
    QStringList path() const
    {
        return mPath;
    }
    QVariantList entries() const
    {
        return mEntries;
    }

signals:
    void changed();

private:
    // Touches nothing when path names no folder.
    void show(const QStringList& path);

    std::optional<ArchiveTree> mTree;
    State mState = Loading;
    Reason mReason = NoReason;
    QStringList mPath;
    QVariantList mEntries;
};
