#include "ClipboardController.h"

#include <QVariantMap>

ClipboardController::ClipboardController(QObject* parent) : QObject(parent) {}

void ClipboardController::copy(const QVariantList& entries, quint64 sourceHandle, bool sourceIsRoot)
{
    take(entries, false, sourceHandle, sourceIsRoot);
}

void ClipboardController::cut(const QVariantList& entries, quint64 sourceHandle, bool sourceIsRoot)
{
    take(entries, true, sourceHandle, sourceIsRoot);
}

std::vector<NodeRef> ClipboardController::toNodeRefs(const QVariantList& entries)
{
    std::vector<NodeRef> converted;
    converted.reserve(static_cast<std::size_t>(entries.size()));
    for (const QVariant& entry : entries)
    {
        const QVariantMap map = entry.toMap();
        converted.push_back(NodeRef{map.value(QStringLiteral("name")).toString().toStdString(),
                                    map.value(QStringLiteral("handle")).toULongLong(),
                                    map.value(QStringLiteral("isFolder")).toBool()});
    }
    return converted;
}

QVariantList ClipboardController::toVariantList(const std::vector<NodeRef>& entries)
{
    QVariantList list;
    list.reserve(static_cast<qsizetype>(entries.size()));
    for (const NodeRef& entry : entries)
    {
        QVariantMap map;
        map[QStringLiteral("handle")] = static_cast<quint64>(entry.handle);
        map[QStringLiteral("name")] = QString::fromStdString(entry.name);
        map[QStringLiteral("isFolder")] = entry.isFolder;
        list.append(map);
    }
    return list;
}

void ClipboardController::take(const QVariantList& entries,
                               bool cut,
                               quint64 sourceHandle,
                               bool sourceIsRoot)
{
    mEntries = toNodeRefs(entries);
    mIsCut = cut;
    mSourceHandle = sourceHandle;
    mSourceIsRoot = sourceIsRoot;
    // Unconditional, even for identical content: a copy replacing a cut has to
    // un-ghost the previously cut rows.
    emit contentChanged();
}

void ClipboardController::clear()
{
    if (mEntries.empty())
        return;

    mEntries.clear();
    mIsCut = false;
    mSourceHandle = 0;
    mSourceIsRoot = true;
    emit contentChanged();
}

bool ClipboardController::canPasteInto(quint64 handle, bool isRoot) const
{
    if (mEntries.empty())
        return false;
    if (!mIsCut)
        return true;

    const bool sameFolder = mSourceIsRoot ? isRoot : (!isRoot && handle == mSourceHandle);
    return !sameFolder;
}

bool ClipboardController::hasContent() const
{
    return !mEntries.empty();
}

bool ClipboardController::isCut() const
{
    return mIsCut;
}

int ClipboardController::count() const
{
    return static_cast<int>(mEntries.size());
}

QVariantList ClipboardController::cutHandles() const
{
    QVariantList handles;
    if (!mIsCut)
        return handles;

    handles.reserve(static_cast<qsizetype>(mEntries.size()));
    // Cast rather than fromValue(entry.handle) directly: QML compares these
    // against a delegate's own handle with JS ===, and std::uint64_t is not
    // quint64 on every platform -- where it maps to unsigned long the metatype
    // would come out ULong and the comparison would silently stop matching.
    for (const NodeRef& entry : mEntries)
        handles.append(QVariant::fromValue(static_cast<quint64>(entry.handle)));
    return handles;
}

const std::vector<NodeRef>& ClipboardController::entries() const
{
    return mEntries;
}

quint64 ClipboardController::sourceHandle() const
{
    return mSourceHandle;
}

bool ClipboardController::sourceIsRoot() const
{
    return mSourceIsRoot;
}
