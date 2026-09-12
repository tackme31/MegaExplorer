#pragma once
#include "core/ThumbnailService.h"

#include <QObject>
#include <QString>
#include <QThreadPool>

#include <cstdint>
#include <memory>
#include <QtQml/qqmlregistration.h>

// The on-disk thumbnail cache as the Settings dialog sees it: how big it is, and a
// way to empty it. Only the signed-in account's own directory is touched, which is
// what keeps two accounts on one machine apart (STUDY_THUMBNAIL_CACHE.md 2-2).
//
// Both answers are read on a pool thread because they enumerate the directory, and
// that is proportional to the number of cached files (2-4). The pool is a member so
// that ~QThreadPool waits for a running job before the QObject it posts back to is
// gone; it holds one thread, so refresh and clear cannot interleave.
class CacheController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided as the cacheController context property")

    // Formatted here rather than in QML, which has no formattedDataSize. Empty means
    // "no answer yet", either because nothing has asked or because the last ask
    // failed -- the dialog words both, since neither is a size.
    Q_PROPERTY(QString sizeText READ sizeText NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)

public:
    explicit CacheController(std::shared_ptr<ThumbnailService> thumbnails,
                             QObject* parent = nullptr);

    QString sizeText() const;
    bool busy() const;

    // Re-reads the size. Called every time the dialog opens rather than once, since
    // signing into another account changes which directory is being measured.
    Q_INVOKABLE void refresh();

    // Empties the cache and reports what is left, so a file that refused to go still
    // shows up as a size instead of silently reading zero.
    Q_INVOKABLE void clear();

signals:
    void changed();

private:
    void apply(const Result<std::uint64_t>& size);

    std::shared_ptr<ThumbnailService> mThumbnails;
    QString mSizeText;
    bool mBusy = false;
    // Declared last so it is destroyed first: ~QThreadPool waits for the running job,
    // and that job touches the members above through the lambda it still holds.
    QThreadPool mPool;
};
