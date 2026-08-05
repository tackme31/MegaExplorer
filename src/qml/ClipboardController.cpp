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

void ClipboardController::take(const QVariantList& entries,
                               bool cut,
                               quint64 sourceHandle,
                               bool sourceIsRoot)
{
    std::vector<Entry> taken;
    taken.reserve(static_cast<std::size_t>(entries.size()));
    for (const QVariant& entry : entries)
    {
        const QVariantMap map = entry.toMap();
        taken.push_back(Entry{map.value(QStringLiteral("handle")).toULongLong(),
                              map.value(QStringLiteral("name")).toString(),
                              map.value(QStringLiteral("isFolder")).toBool()});
    }

    mEntries = std::move(taken);
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
    for (const Entry& entry : mEntries)
        handles.append(QVariant::fromValue(entry.handle));
    return handles;
}

const std::vector<ClipboardController::Entry>& ClipboardController::entries() const
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
