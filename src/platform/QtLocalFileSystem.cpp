#include "QtLocalFileSystem.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QString>

#include <filesystem>
#include <string>
#include <system_error>
#include <windows.h>

namespace
{
// QSaveFile is the port's temporary-name-then-rename, and its destructor discards
// an uncommitted file. It never needs an event loop, so the SDK thread may own it.
class QtLocalFileWriter final : public ILocalFileWriter
{
public:
    explicit QtLocalFileWriter(const QString& path)
        : mFile(path)
    {}

    bool open()
    {
        return mFile.open(QIODevice::WriteOnly);
    }

    bool write(const char* data, std::size_t size) override
    {
        return mFile.write(data, static_cast<qint64>(size)) == static_cast<qint64>(size);
    }

    bool commit() override
    {
        return mFile.commit();
    }

private:
    QSaveFile mFile;
};

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

std::unique_ptr<ILocalFileWriter> QtLocalFileSystem::createFile(const std::string& path)
{
    auto writer = std::make_unique<QtLocalFileWriter>(QString::fromStdString(path));
    if (!writer->open())
        return nullptr;
    return writer;
}

bool QtLocalFileSystem::createDirectory(const std::string& path)
{
    // mkpath, not mkdir: it creates the missing parents and reports true for a
    // directory that is already there, which is what the port promises.
    return QDir().mkpath(QString::fromStdString(path));
}

std::optional<LocalEntry> QtLocalFileSystem::entryFor(const std::string& path) const
{
    const QFileInfo info(QString::fromStdString(path));
    // exists() is false for a symlink whose target is gone, but the name is
    // taken on disk all the same, so it still has to be reported.
    if (!info.exists() && !info.isSymLink())
        return std::nullopt;
    return toEntry(info);
}

std::optional<std::string> QtLocalFileSystem::moveToFreeName(const std::string& from,
                                                             const std::string& to)
{
    const std::wstring source = QString::fromStdString(from).toStdWString();
    const QFileInfo target(QString::fromStdString(to));
    const QString leaf = target.fileName();
    // The SDK's FileNameGenerator splits at the last dot, so "a.tar.gz" becomes
    // "a.tar (1).gz" and ".bashrc" becomes " (1).bashrc".
    const qsizetype dot = leaf.lastIndexOf('.');
    const QString stem = dot < 0 ? leaf : leaf.left(dot);
    const QString extension = dot < 0 ? QString() : leaf.mid(dot);
    const QDir dir = target.dir();

    constexpr int kMaxSuffix = 10000;
    for (int n = 0; n <= kMaxSuffix; ++n)
    {
        const QString candidate =
            n == 0 ? target.filePath()
                   : dir.filePath(stem + QStringLiteral(" (%1)").arg(n) + extension);
        const QString native = QDir::toNativeSeparators(candidate);
        // Without MOVEFILE_REPLACE_EXISTING a taken name fails in the same step that would
        // claim it. Not QFile::rename: it checks first, then may fall back to a copy.
        if (::MoveFileExW(source.c_str(), native.toStdWString().c_str(), 0))
            return native.toStdString();
        const QFileInfo taken(candidate);
        if (!taken.exists() && !taken.isSymLink())
            break; // failed for some other reason than the name
    }
    ::DeleteFileW(source.c_str());
    return std::nullopt;
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
