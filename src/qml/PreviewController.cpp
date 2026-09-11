#include "PreviewController.h"

#include "app/Logging.h"
#include "ArchiveTree.h"
#include "core/PreviewKind.h"
#include "core/ZipListing.h"
#include "GuiThread.h"
#include "TextPreviewDecoder.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <algorithm>
#include <optional>
#include <utility>

PreviewController::PreviewController(std::shared_ptr<PreviewService> service,
                                     std::shared_ptr<PreviewImageStore> imageStore,
                                     QObject* parent)
    : QObject(parent), mService(std::move(service)), mImageStore(std::move(imageStore))
{}

void PreviewController::showSelection(quint64 handle,
                                      const QString& name,
                                      qulonglong sizeBytes,
                                      bool isFolder)
{
    // Re-clicking the selected row, or switching to a tab whose selection is the
    // same node, re-emits selectionChanged, and re-fetching would throw away a
    // preview that is already on screen. clear() resets this, so hiding and
    // re-showing the pane still refetches.
    if (mShownHandle && *mShownHandle == handle)
        return;
    mShownHandle = handle;

    ++mGeneration;
    // Moving to a folder or an unpreviewable file issues no request of its own, so
    // the previous one has to be stopped here rather than by being superseded.
    mService->cancel();
    mImageStore->clear();

    if (isFolder)
    {
        publish(Empty, NoKind, NoReason);
        return;
    }

    switch (previewKindForName(name.toStdString()))
    {
        // All three arrive as the one server-generated JPEG, so the pane makes no
        // distinction; only the in-app viewer does.
        case PreviewKind::Image:
        case PreviewKind::Video:
        case PreviewKind::Pdf:
            requestImage(handle);
            return;
        case PreviewKind::Text:
            requestText(handle, sizeBytes);
            return;
        case PreviewKind::Archive:
            requestArchive(handle, sizeBytes);
            return;
        // Audio is classified for the viewer's sake only; the server generates no
        // preview for it, so the pane says the same thing it says for anything else.
        case PreviewKind::Audio:
        case PreviewKind::None:
            publish(Unsupported, NoKind, UnsupportedType);
            return;
    }
}

void PreviewController::clear()
{
    mShownHandle.reset();
    ++mGeneration;
    mService->cancel();
    mImageStore->clear();
    publish(Empty, NoKind, NoReason);
}

void PreviewController::requestImage(quint64 handle)
{
    publish(Loading, NoKind, NoReason);

    const quint64 generation = mGeneration;
    const QString destinationPath = computeDestinationPath(handle, generation);
    mService->request(handle,
                      destinationPath.toStdString(),
                      [this, generation, destinationPath](Result<std::string> result) {
                          invokeOnGuiThread(
                              this,
                              [this, generation, destinationPath, result = std::move(result)]() {
                                  onImageFetched(generation, destinationPath, result);
                              });
                      });
}

void PreviewController::onImageFetched(quint64 generation,
                                       const QString& destinationPath,
                                       Result<std::string> result)
{
    // Read and delete before looking at the generation: the file exists only to get
    // the bytes out of the SDK, so a superseded result must still take its file with
    // it. A failed request wrote nothing and remove() simply says so.
    const QString writtenPath = result.success && !result.value().empty()
                                    ? QString::fromStdString(result.value())
                                    : destinationPath;
    QByteArray jpeg;
    QFile file(writtenPath);
    if (file.open(QIODevice::ReadOnly))
    {
        jpeg = file.readAll();
        file.close();
    }
    QFile::remove(writtenPath);

    if (generation != mGeneration)
        return;

    if (!result.success || jpeg.isEmpty())
    {
        // Debug, not warning: most files simply have no preview stored, which is the
        // same reason this controller has no NotificationController to report to.
        qCDebug(lcPreview) << "no preview for" << writtenPath
                           << QString::fromStdString(result.errorMessage)
                           << "code=" << result.errorCode;
        publish(Unsupported, NoKind, NoPreviewAvailable);
        return;
    }

    mImageStore->set(generation, std::move(jpeg));
    mImageSource = QStringLiteral("image://megapreview/%1").arg(generation);
    publish(Ready, Image, NoReason);
}

void PreviewController::requestText(quint64 handle, qulonglong sizeBytes)
{
    // Refused before any request: sizeBytes came with the listing, so a 100 MB log
    // costs nothing at all to turn down.
    if (sizeBytes > kMaxTextPreviewBytes)
    {
        publish(Unsupported, NoKind, TooLarge);
        return;
    }

    publish(Loading, NoKind, NoReason);

    const quint64 generation = mGeneration;
    mService->requestText(
        handle, kMaxTextPreviewBytes, [this, generation](Result<std::vector<char>> result) {
            invokeOnGuiThread(this, [this, generation, result = std::move(result)]() {
                onTextFetched(generation, result);
            });
        });
}

void PreviewController::onTextFetched(quint64 generation, Result<std::vector<char>> result)
{
    if (generation != mGeneration)
        return;

    if (!result.success)
    {
        qCDebug(lcPreview) << "text fetch failed:" << QString::fromStdString(result.errorMessage)
                           << "code=" << result.errorCode;
        publish(Unsupported, NoKind, NoPreviewAvailable);
        return;
    }

    const std::vector<char>& bytes = result.value();
    const std::optional<QString> decoded =
        decodePreviewText(QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())));
    if (!decoded)
    {
        publish(Unsupported, NoKind, BinaryContent);
        return;
    }

    mText = *decoded;
    publish(Ready, Text, NoReason);
}

// Two round trips per selection, every time: the listing is not cached, matching
// the rest of this controller. Re-selecting the same zip costs both reads again.
void PreviewController::requestArchive(quint64 handle, qulonglong sizeBytes)
{
    // The End Of Central Directory record cannot exist below this, so there is
    // nothing to look for.
    if (sizeBytes < 22)
    {
        publish(Unsupported, NoKind, ArchiveUnreadable);
        return;
    }

    publish(Loading, NoKind, NoReason);

    mArchiveHandle = handle;
    mArchiveSize = sizeBytes;
    const quint64 tailLength = std::min<quint64>(sizeBytes, kZipTailScanBytes);
    mArchiveTailOffset = sizeBytes - tailLength;

    const quint64 generation = mGeneration;
    mService->requestRange(handle,
                           mArchiveTailOffset,
                           tailLength,
                           [this, generation](Result<std::vector<char>> result) {
                               invokeOnGuiThread(this,
                                                 [this, generation, result = std::move(result)]() {
                                                     onArchiveTailFetched(generation, result);
                                                 });
                           });
}

void PreviewController::onArchiveTailFetched(quint64 generation, Result<std::vector<char>> result)
{
    if (generation != mGeneration)
        return;

    if (!result.success)
    {
        qCDebug(lcPreview) << "archive tail fetch failed:"
                           << QString::fromStdString(result.errorMessage)
                           << "code=" << result.errorCode;
        publish(Unsupported, NoKind, NoPreviewAvailable);
        return;
    }

    const std::optional<ZipDirectoryLocation> location =
        findZipDirectory(result.value(), mArchiveTailOffset);
    if (!location || location->offset >= mArchiveSize)
    {
        publish(Unsupported, NoKind, ArchiveUnreadable);
        return;
    }
    // A validated record that describes a zero-byte directory is an archive with no
    // entries, not a broken one -- saying "could not be read" would be wrong.
    if (location->size == 0)
    {
        publish(Unsupported, NoKind, ArchiveEmpty);
        return;
    }

    // The tail slice usually contains the directory already, but it is re-read
    // rather than sliced out: one extra range read against one code path beats two
    // paths whose bounds arithmetic has to agree.
    mService->requestRange(mArchiveHandle,
                           location->offset,
                           location->size,
                           [this, generation](Result<std::vector<char>> directory) {
                               invokeOnGuiThread(
                                   this, [this, generation, directory = std::move(directory)]() {
                                       onArchiveDirectoryFetched(generation, directory);
                                   });
                           });
}

void PreviewController::onArchiveDirectoryFetched(quint64 generation,
                                                  Result<std::vector<char>> result)
{
    if (generation != mGeneration)
        return;

    if (!result.success)
    {
        qCDebug(lcPreview) << "archive directory fetch failed:"
                           << QString::fromStdString(result.errorMessage)
                           << "code=" << result.errorCode;
        publish(Unsupported, NoKind, NoPreviewAvailable);
        return;
    }

    const std::vector<ZipEntry> entries = parseZipDirectory(result.value());
    if (entries.empty())
    {
        publish(Unsupported, NoKind, ArchiveUnreadable);
        return;
    }

    mArchiveEntries = ArchiveTree(entries).flattened();
    publish(Ready, Archive, NoReason);
}

void PreviewController::publish(State state, Kind kind, Reason reason)
{
    mState = state;
    mKind = kind;
    mReason = reason;
    if (kind != Image)
        mImageSource.clear();
    if (kind != Text)
        mText.clear();
    if (kind != Archive)
        mArchiveEntries.clear();
    emit changed();
}

QString PreviewController::computeDestinationPath(quint64 handle, quint64 generation) const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/" +
                        QCoreApplication::applicationName() + "Previews";
    QDir().mkpath(dir);
    // Native separators are required, not cosmetic: the SDK's localpath.cpp splits
    // on '\' on Windows. Same rule as ThumbnailController's path.
    return QDir::toNativeSeparators(
        QStringLiteral("%1/%2-%3.jpg").arg(dir).arg(handle).arg(generation));
}
