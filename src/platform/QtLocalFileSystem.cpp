#include "QtLocalFileSystem.h"

#include <QDir>
#include <QFileInfo>
#include <QString>

#include <filesystem>
#include <system_error>

namespace
{
LocalEntry toEntry(const QFileInfo& info)
{
    LocalEntry entry;
    // Native separators because the path crosses into the SDK's LocalPath, which
    // splits on '\' on Windows.
    entry.path = QDir::toNativeSeparators(info.absoluteFilePath()).toStdString();
    entry.name = info.fileName().toStdString();
    // Qt reports a Windows .lnk as whatever it points at, so a shortcut to a
    // folder would otherwise be walked as that folder -- while the SDK uploads
    // the .lnk itself, as an ordinary file. Its size is the target's too, hence
    // left at 0 rather than reported wrong.
    entry.isDirectory = info.isDir() && !info.isShortcut();
    if (!entry.isDirectory && !info.isShortcut())
        entry.sizeBytes = static_cast<std::uint64_t>(info.size());
    return entry;
}
} // namespace

std::optional<LocalEntry> QtLocalFileSystem::entryFor(const std::string& path) const
{
    const QFileInfo info(QString::fromStdString(path));
    // exists() is false for a symlink whose target is gone, but the name is
    // taken on disk all the same, so it still has to be reported.
    if (!info.exists() && !info.isSymLink())
        return std::nullopt;
    return toEntry(info);
}

std::optional<std::vector<LocalEntry>>
QtLocalFileSystem::listDirectory(const std::string& path) const
{
    const QString nativePath = QString::fromStdString(path);
    if (!QFileInfo(nativePath).isDir())
        return std::nullopt;
    const QDir dir(nativePath);

    // Hidden because the SDK's recursive upload sends hidden files, System
    // because entryInfoList otherwise drops symlinks with a missing target: a
    // scan that matched Explorer's view instead would under-count collisions.
    const QFileInfoList children = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden |
                                                     QDir::System | QDir::NoDotAndDotDot);
    // entryInfoList reports a refused directory as an empty one, and QDir's own
    // isReadable() cannot tell them apart either -- on Windows it ignores NTFS
    // ACLs unless qt_ntfs_permission_lookup is enabled. The iterator does the
    // FindFirstFile itself and surfaces the error, so probe with it, but only in
    // the empty case, where it costs one call and nothing is being listed twice.
    if (children.isEmpty())
    {
        std::error_code ec;
        // Constructed from a wide string: std::filesystem reads a narrow one in
        // the ANSI code page, which mangles anything outside it.
        const std::filesystem::directory_iterator probe(
            std::filesystem::path(nativePath.toStdWString()), ec);
        if (ec)
            return std::nullopt;
    }

    std::vector<LocalEntry> entries;
    entries.reserve(static_cast<std::size_t>(children.size()));
    for (const QFileInfo& info : children)
        entries.push_back(toEntry(info));
    return entries;
}
