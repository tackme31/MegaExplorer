#include "ViewerController.h"

#include "app/Logging.h"
#include "ArchiveTree.h"
#include "core/PreviewKind.h"
#include "core/ZipListing.h"
#include "GuiThread.h"

#include <QJSEngine>

#include <algorithm>
#include <optional>
#include <utility>

ViewerController::ViewerController(std::shared_ptr<IMegaClient> client, QObject* parent)
    : QObject(parent), mClient(std::move(client))
{}

QString ViewerController::viewerKind(const QString& name) const
{
    switch (previewKindForName(name.toStdString()))
    {
        case PreviewKind::Image:
            return QStringLiteral("image");
        case PreviewKind::Video:
            return QStringLiteral("video");
        case PreviewKind::Pdf:
            return QStringLiteral("pdf");
        case PreviewKind::Audio:
            return QStringLiteral("audio");
        case PreviewKind::Archive:
            return QStringLiteral("archive");
        // The rest never get one.
        case PreviewKind::Text:
        case PreviewKind::None:
            break;
    }
    return {};
}

QString ViewerController::sourceUrl(quint64 handle)
{
    Result<std::string> url = mClient->streamingUrl(handle);
    if (!url.success)
        return {};
    return QString::fromStdString(url.value());
}

ArchiveBrowser* ViewerController::openArchive(quint64 handle, qulonglong sizeBytes, QObject* owner)
{
    auto* browser = new ArchiveBrowser(owner);
    // A QObject* handed back to QML is otherwise the collector's to free, on its own
    // schedule; the window's parentage is what should decide.
    QJSEngine::setObjectOwnership(browser, QJSEngine::CppOwnership);

    // The End Of Central Directory record cannot exist below this.
    if (sizeBytes < 22)
    {
        browser->fail(ArchiveBrowser::Unreadable);
        return browser;
    }

    const quint64 tailLength = std::min<quint64>(sizeBytes, kZipTailScanBytes);
    const quint64 tailOffset = sizeBytes - tailLength;
    // Guarded rather than raw: the window may close before either read lands.
    const QPointer<ArchiveBrowser> target(browser);
    mClient->readFileRange(
        handle,
        tailOffset,
        tailLength,
        [this, target, handle, sizeBytes, tailOffset](Result<std::vector<char>> result) {
            invokeOnGuiThread(
                this,
                [this, target, handle, sizeBytes, tailOffset, result = std::move(result)]() mutable {
                    onArchiveTailFetched(target, handle, sizeBytes, tailOffset, std::move(result));
                });
        });
    return browser;
}

void ViewerController::onArchiveTailFetched(const QPointer<ArchiveBrowser>& browser,
                                            quint64 handle,
                                            quint64 sizeBytes,
                                            quint64 tailOffset,
                                            Result<std::vector<char>> result)
{
    if (!browser)
        return;

    if (!result.success)
    {
        qCDebug(lcPreview) << "archive viewer tail fetch failed:"
                           << QString::fromStdString(result.errorMessage)
                           << "code=" << result.errorCode;
        browser->fail(ArchiveBrowser::FetchFailed);
        return;
    }

    const std::optional<ZipDirectoryLocation> location =
        findZipDirectory(result.value(), tailOffset);
    if (!location || location->offset >= sizeBytes)
    {
        browser->fail(ArchiveBrowser::Unreadable);
        return;
    }
    if (location->size == 0)
    {
        browser->fail(ArchiveBrowser::Empty);
        return;
    }

    const QPointer<ArchiveBrowser> target = browser;
    const quint64 shift = location->localHeaderShift;
    mClient->readFileRange(
        handle,
        location->offset,
        location->size,
        [this, target, handle, shift](Result<std::vector<char>> directory) {
            invokeOnGuiThread(
                this, [this, target, handle, shift, directory = std::move(directory)]() mutable {
                    onArchiveDirectoryFetched(target, handle, shift, std::move(directory));
                });
        });
}

void ViewerController::onArchiveDirectoryFetched(const QPointer<ArchiveBrowser>& browser,
                                                 quint64 handle,
                                                 quint64 localHeaderShift,
                                                 Result<std::vector<char>> result)
{
    if (!browser)
        return;

    if (!result.success)
    {
        qCDebug(lcPreview) << "archive viewer directory fetch failed:"
                           << QString::fromStdString(result.errorMessage)
                           << "code=" << result.errorCode;
        browser->fail(ArchiveBrowser::FetchFailed);
        return;
    }

    const std::vector<ZipEntry> entries = parseZipDirectory(result.value());
    if (entries.empty())
    {
        browser->fail(ArchiveBrowser::Unreadable);
        return;
    }
    browser->setTree(ArchiveTree(entries), handle, localHeaderShift);
}
