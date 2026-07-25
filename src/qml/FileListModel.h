#pragma once
#include "core/FileEntry.h"

#include <QAbstractListModel>

#include <vector>

// Owned by main.cpp's composition root and exposed to QML via
// setContextProperty(); not instantiated from QML, so no QML_ELEMENT needed.
class FileListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role
    {
        NameRole = Qt::UserRole + 1,
        SizeRole,
        IsFolderRole,
        HandleRole,
        HasThumbnailRole,
        ThumbnailPathRole,
    };

    explicit FileListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setEntries(std::vector<FileEntry> entries);

    // Updates just the thumbnail-path role for handle's row (no full model
    // reset, unlike setEntries) so a grid view doesn't flicker/relayout when
    // a thumbnail arrives asynchronously. No-op if handle's row is no longer
    // present (e.g. the user navigated away before the fetch completed).
    void setThumbnailPath(quint64 handle, QString path);

private:
    std::vector<FileEntry> mEntries;
    // Parallel to mEntries (same index, resized alongside it in setEntries).
    // Kept out of FileEntry itself since it's a session-local, GUI-populated
    // cache result, not SDK domain data -- FileEntry stays Qt-free.
    std::vector<QString> mThumbnailPaths;
};
