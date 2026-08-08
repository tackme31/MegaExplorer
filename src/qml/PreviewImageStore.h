#pragma once
#include <QByteArray>
#include <QMutex>
#include <QMutexLocker>

#include <utility>

// The one preview JPEG currently on show, held in memory rather than on disk.
//
// The SDK can only write a preview to a file -- there is no memory-returning
// attribute API -- so PreviewController reads that file back and deletes it inside
// the same callback, and the bytes land here. Nothing survives the next selection.
//
// Shared between PreviewController and PreviewImageProvider because the QML engine
// takes ownership of the provider, so the provider cannot own the state the
// controller writes.
//
// The mutex is not optional: QQuickImageProvider::requestImage runs on Qt's
// image-loading thread whenever the Image asking for it is asynchronous, which it is.
class PreviewImageStore
{
public:
    void set(quint64 generation, QByteArray jpeg)
    {
        QMutexLocker locker(&mMutex);
        mGeneration = generation;
        mJpeg = std::move(jpeg);
    }

    void clear()
    {
        QMutexLocker locker(&mMutex);
        mJpeg.clear();
    }

    // Empty unless generation still names what is stored -- a request for any other
    // generation is stale by definition.
    QByteArray bytesFor(quint64 generation) const
    {
        QMutexLocker locker(&mMutex);
        return mGeneration == generation ? mJpeg : QByteArray();
    }

private:
    mutable QMutex mMutex;
    quint64 mGeneration = 0;
    QByteArray mJpeg;
};
