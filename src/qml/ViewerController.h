#pragma once
#include "core/IMegaClient.h"

#include <QObject>
#include <QString>

#include <memory>
#include <QtQml/qqmlregistration.h>

// Backs the in-app viewer: says which viewer a name belongs to, and turns a node into
// a URL Qt Quick's Image or Qt Multimedia's MediaPlayer can load.
//
// Distinct from PreviewController, which drives the side pane: that one shows the
// small server-generated JPEG, this one the original bytes. No NotificationController,
// for PreviewController's reason -- "this file cannot be shown" is wording the viewer
// itself carries, not a toast.
class ViewerController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided as the viewerController context property")

public:
    explicit ViewerController(std::shared_ptr<IMegaClient> client, QObject* parent = nullptr);

    // "image", "video", or empty for anything the viewer cannot open yet. Extension-only,
    // the same basis PreviewKind classifies on. A string rather than an enum because the
    // only consumer is QML, which uses it to pick which viewer component to create.
    Q_INVOKABLE QString viewerKind(const QString& name) const;

    // Local HTTP URL for the node's original bytes, empty when the server refused to
    // start. Never log the result: the URL is a capability (IMegaClient.h).
    Q_INVOKABLE QString sourceUrl(quint64 handle);

private:
    std::shared_ptr<IMegaClient> mClient;
};
