#include "CacheController.h"

#include "GuiThread.h"

#include <QLocale>

#include <utility>

CacheController::CacheController(std::shared_ptr<ThumbnailService> thumbnails, QObject* parent)
    : QObject(parent), mThumbnails(std::move(thumbnails))
{
    mPool.setMaxThreadCount(1);
}

QString CacheController::sizeText() const
{
    return mSizeText;
}

bool CacheController::busy() const
{
    return mBusy;
}

void CacheController::refresh()
{
    if (mBusy)
        return;
    mBusy = true;
    emit changed();
    mPool.start([this, thumbnails = mThumbnails] {
        Result<std::uint64_t> size = thumbnails->cachedBytes();
        invokeOnGuiThread(this, [this, size = std::move(size)] {
            apply(size);
        });
    });
}

void CacheController::clear()
{
    if (mBusy)
        return;
    mBusy = true;
    emit changed();
    mPool.start([this, thumbnails = mThumbnails] {
        const Result<void> cleared = thumbnails->clearCache();
        Result<std::uint64_t> size = thumbnails->cachedBytes();
        // A size read that succeeded after a failed delete would report "0 bytes" for
        // files that are still there, so the delete's verdict wins.
        if (!cleared.success)
            size = Result<std::uint64_t>::fail(cleared.errorMessage, cleared.errorCode);
        invokeOnGuiThread(this, [this, size = std::move(size)] {
            apply(size);
        });
    });
}

void CacheController::apply(const Result<std::uint64_t>& size)
{
    // c() not system(): the unit word is locale data and the UI is English-only, the
    // same choice the file list's sizes make.
    mSizeText = size.success ? QLocale::c().formattedDataSize(static_cast<qint64>(size.value()),
                                                              1,
                                                              QLocale::DataSizeTraditionalFormat)
                             : QString();
    mBusy = false;
    emit changed();
}
