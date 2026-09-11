#pragma once
#include "core/ZipListing.h"

#include <QString>
#include <QStringList>
#include <QVariantList>

#include <map>
#include <optional>
#include <vector>

// A zip's flat list of paths folded into folders, for the two places that show one:
// the preview pane (every row, indented) and the archive viewer (one folder at a
// time). Names are decoded here because picking between UTF-8 and CP932 needs Qt's
// codecs, which ZipListing deliberately does not link.
class ArchiveTree
{
public:
    explicit ArchiveTree(const std::vector<ZipEntry>& entries);

    // Depth-first, folders before files at each level. Each row: name, depth,
    // isDirectory, formattedSize.
    QVariantList flattened() const;

    // The rows directly inside the folder that path names, root when empty, in the
    // same order. Each row: name, isDirectory, formattedSize, and for a file
    // extractable plus blockedReason ("encrypted", "unsupportedMethod" or empty).
    // Nothing when path does not name a folder.
    std::optional<QVariantList> folderRows(const QStringList& path) const;

    // The entry behind the file row called name inside the folder path names.
    std::optional<ZipEntry> fileEntry(const QStringList& path, const QString& name) const;

private:
    struct Node
    {
        std::map<QString, Node> children;
        bool isDirectory = false;
        bool hasFile = false;
        quint64 size = 0;
        std::size_t entryIndex = 0; // into mEntries; meaningful when hasFile
    };

    void insertPath(const QString& path, bool isDirectory, quint64 size, std::size_t entryIndex);
    const Node* folderAt(const QStringList& path) const;
    static void flatten(const Node& node, int depth, QVariantList& rows);

    Node mRoot;
    std::vector<ZipEntry> mEntries;
};
