#include "PreviewController.h"

#include "app/Logging.h"
#include "core/PreviewKind.h"
#include "GuiThread.h"
#include "TextPreviewDecoder.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <optional>

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
    mImageStore->clear();

    if (isFolder)
    {
        publish(Empty, NoKind, NoReason);
        return;
    }

    switch (previewKindForName(name.toStdString()))
    {
        case PreviewKind::Image:
            requestImage(handle);
            return;
        case PreviewKind::Text:
            requestText(handle, sizeBytes);
            return;
        case PreviewKind::None:
            publish(Unsupported, NoKind, UnsupportedType);
            return;
    }
}

void PreviewController::clear()
{
    mShownHandle.reset();
    ++mGeneration;
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
    mService->requestText(handle,
                          kMaxTextPreviewBytes,
                          [this, generation](Result<std::vector<char>> result) {
                              invokeOnGuiThread(this,
                                                [this, generation, result = std::move(result)]() {
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
        qCDebug(lcPreview) << "text fetch failed:"
                           << QString::fromStdString(result.errorMessage)
                           << "code=" << result.errorCode;
        publish(Unsupported, NoKind, NoPreviewAvailable);
        return;
    }

    const std::vector<char>& bytes = result.value();
    const std::optional<QString> decoded = decodePreviewText(
        QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())));
    if (!decoded)
    {
        publish(Unsupported, NoKind, BinaryContent);
        return;
    }

    mText = *decoded;
    publish(Ready, Text, NoReason);
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
    emit changed();
}

QString PreviewController::computeDestinationPath(quint64 handle, quint64 generation) const
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/MegaExplorerPreviews";
    QDir().mkpath(dir);
    // Native separators are required, not cosmetic: the SDK's localpath.cpp splits
    // on '\' on Windows. Same rule as ThumbnailController's path.
    return QDir::toNativeSeparators(
        QStringLiteral("%1/%2-%3.jpg").arg(dir).arg(handle).arg(generation));
}
