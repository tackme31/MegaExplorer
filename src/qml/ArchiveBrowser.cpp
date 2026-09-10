#include "ArchiveBrowser.h"

#include <utility>

ArchiveBrowser::ArchiveBrowser(QObject* parent) : QObject(parent) {}

void ArchiveBrowser::setTree(ArchiveTree tree)
{
    mTree = std::move(tree);
    mState = Ready;
    mReason = NoReason;
    show({});
}

void ArchiveBrowser::fail(Reason reason)
{
    mTree.reset();
    mState = Failed;
    mReason = reason;
    mPath.clear();
    mEntries.clear();
    emit changed();
}

void ArchiveBrowser::openFolder(const QString& name)
{
    QStringList next = mPath;
    next.append(name);
    show(next);
}

void ArchiveBrowser::goUp()
{
    if (!mPath.isEmpty())
        goToDepth(static_cast<int>(mPath.size()) - 1);
}

void ArchiveBrowser::goToDepth(int depth)
{
    if (depth < 0 || depth > mPath.size())
        return;
    show(mPath.mid(0, depth));
}

void ArchiveBrowser::show(const QStringList& path)
{
    if (!mTree)
        return;
    std::optional<QVariantList> rows = mTree->folderRows(path);
    if (!rows)
        return;
    mPath = path;
    mEntries = std::move(*rows);
    emit changed();
}
