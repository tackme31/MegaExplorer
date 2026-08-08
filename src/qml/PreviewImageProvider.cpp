#include "PreviewImageProvider.h"

#include <QImage>

PreviewImageProvider::PreviewImageProvider(std::shared_ptr<PreviewImageStore> store)
    : QQuickImageProvider(QQuickImageProvider::Image), mStore(std::move(store))
{}

QImage PreviewImageProvider::requestImage(const QString& id, QSize* size, const QSize&)
{
    bool ok = false;
    const quint64 generation = id.toULongLong(&ok);
    const QByteArray jpeg = ok ? mStore->bytesFor(generation) : QByteArray();

    QImage image;
    // A miss means the selection moved on between the URL being set and this thread
    // getting to it. The Image goes to its error state, which is invisible: QML has
    // already pointed it at the newer generation.
    if (!jpeg.isEmpty())
        image.loadFromData(jpeg);

    if (size)
        *size = image.size();
    return image;
}
