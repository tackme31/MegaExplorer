#pragma once
#include "PreviewImageStore.h"

#include <QQuickImageProvider>

#include <memory>

// Serves PreviewPane's Image from PreviewImageStore under image://megapreview/<n>,
// where <n> is the request generation. A file:// URL would not do: QQuickPixmapCache
// keys on the URL, so reusing one path for every preview shows the first picture
// forever.
//
// The QML engine takes ownership of whatever is handed to addImageProvider, hence
// the shared store rather than state held here.
class PreviewImageProvider : public QQuickImageProvider
{
public:
    explicit PreviewImageProvider(std::shared_ptr<PreviewImageStore> store);

    // Runs on Qt's image-loading thread, not the GUI thread -- which is the point,
    // since this is where the JPEG is decoded.
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    std::shared_ptr<PreviewImageStore> mStore;
};
