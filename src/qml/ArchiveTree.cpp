#include "ArchiveTree.h"

#include <QByteArray>
#include <QLocale>
#include <QStringDecoder>
#include <QVariantMap>

namespace
{

// Entry names carry no reliable encoding tag: bit 11 promises UTF-8, but Windows
// zips routinely store CP932 without setting it. Same fallback ladder as
// TextPreviewDecoder.
QString decodeEntryName(const std::string& raw, bool declaredUtf8)
{
    const QByteArray bytes(raw.data(), static_cast<qsizetype>(raw.size()));
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString decoded = utf8.decode(bytes);
    if (declaredUtf8 || !utf8.hasError())
        return decoded;

    QStringDecoder shiftJis("Shift-JIS");
    if (shiftJis.isValid())
        return shiftJis.decode(bytes);
    return QStringDecoder(QStringDecoder::System).decode(bytes);
}

// A name may be 65535 bytes, so "a/" repeated makes an archive whose tree is tens of
// thousands deep -- enough for flatten()'s recursion to run out of stack. Anything
// past this is dropped rather than nested further.
constexpr qsizetype kMaxArchiveDepth = 64;

QVariantMap makeRow(const QString& name, bool isDirectory, quint64 size)
{
    QVariantMap row;
    row.insert("name", name);
    row.insert("isDirectory", isDirectory);
    // Traditional rather than Iec is what the file list and the properties dialog
    // chose: 1024-based, spelled "kB". c() not system(): the UI is English-only.
    row.insert("formattedSize",
               isDirectory ? QString()
                           : QLocale::c().formattedDataSize(
                                 static_cast<qint64>(size), 1, QLocale::DataSizeTraditionalFormat));
    return row;
}

} // namespace

ArchiveTree::ArchiveTree(const std::vector<ZipEntry>& entries)
{
    for (const ZipEntry& entry : entries)
    {
        insertPath(decodeEntryName(entry.rawName, entry.nameIsUtf8),
                   entry.isDirectory,
                   entry.uncompressedSize);
    }
}

void ArchiveTree::insertPath(const QString& path, bool isDirectory, quint64 size)
{
    QStringList parts = path.split('/', Qt::SkipEmptyParts);
    const bool truncated = parts.size() > kMaxArchiveDepth;
    if (truncated)
        parts = parts.mid(0, kMaxArchiveDepth);
    Node* node = &mRoot;
    for (qsizetype i = 0; i < parts.size(); ++i)
    {
        node = &node->children[parts.at(i)];
        if (i + 1 < parts.size())
            node->isDirectory = true;
    }
    if (node == &mRoot)
        return;
    // What is left of a truncated path names an ancestor, never the entry itself, so
    // its size would be somebody else's.
    if (isDirectory || truncated)
    {
        node->isDirectory = true;
    }
    else
    {
        node->hasFile = true;
        node->size = size;
    }
}

QVariantList ArchiveTree::flattened() const
{
    QVariantList rows;
    flatten(mRoot, 0, rows);
    return rows;
}

void ArchiveTree::flatten(const Node& node, int depth, QVariantList& rows)
{
    // Folders before files at each level, matching the file views' own ordering.
    for (int pass = 0; pass < 2; ++pass)
    {
        for (const auto& [name, child] : node.children)
        {
            const bool isDirectory = child.isDirectory || !child.hasFile;
            if ((pass == 0) != isDirectory)
                continue;
            QVariantMap row = makeRow(name, isDirectory, child.size);
            row.insert("depth", depth);
            rows.append(row);
            flatten(child, depth + 1, rows);
        }
    }
}

std::optional<QVariantList> ArchiveTree::folderRows(const QStringList& path) const
{
    const Node* node = &mRoot;
    for (const QString& part : path)
    {
        const auto found = node->children.find(part);
        if (found == node->children.end())
            return std::nullopt;
        const Node& child = found->second;
        if (!child.isDirectory && child.hasFile)
            return std::nullopt;
        node = &child;
    }

    QVariantList rows;
    for (int pass = 0; pass < 2; ++pass)
    {
        for (const auto& [name, child] : node->children)
        {
            const bool isDirectory = child.isDirectory || !child.hasFile;
            if ((pass == 0) == isDirectory)
                rows.append(makeRow(name, isDirectory, child.size));
        }
    }
    return rows;
}
