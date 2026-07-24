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
    };

    explicit FileListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setEntries(std::vector<FileEntry> entries);

private:
    std::vector<FileEntry> mEntries;
};
