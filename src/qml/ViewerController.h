#pragma once
#include "ArchiveBrowser.h"
#include "core/IMegaClient.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>
#include <QtQml/qqmlregistration.h>
#include <vector>

// Backs the in-app viewer: says which viewer a name belongs to, and turns a node into
// a URL Qt Quick's Image or Qt Multimedia's MediaPlayer can load, or into the listing
// an archive viewer walks.
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

    // "image", "video", "pdf", "audio", "archive", or empty for anything the viewer
    // cannot open yet. Extension-only, the same basis PreviewKind classifies on. A
    // string rather than an enum because the only consumer is QML, which uses it to
    // pick which viewer component to create.
    Q_INVOKABLE QString viewerKind(const QString& name) const;

    // Local HTTP URL for the node's original bytes, empty when the server refused to
    // start. Never log the result: the URL is a capability (IMegaClient.h).
    Q_INVOKABLE QString sourceUrl(quint64 handle);

    // Hands sourceUrl()'s answer to Edge, the one browser Windows ships. The URL is
    // built and spent here rather than returned to QML: it is a capability, so the
    // fewer places hold it the better.
    Q_INVOKABLE void openInBrowser(quint64 handle);

    // A browser that starts Loading and fills itself from two range reads of the zip.
    // owner is the window showing it and becomes its parent, so closing that window
    // is what frees it; reads still in flight then land on nothing.
    //
    // Reads the client directly rather than through PreviewService: that queue is
    // latest-wins, so the pane selecting another file would discard this window's read.
    Q_INVOKABLE ArchiveBrowser* openArchive(quint64 handle, qulonglong sizeBytes, QObject* owner);

    // Open as: reads the file's first bytes and answers with formatChecked, which
    // hands the request back so QML keeps no table of pending ones. kind is a
    // viewerKind() name.
    Q_INVOKABLE void checkFormat(quint64 handle,
                                 const QString& name,
                                 qulonglong sizeBytes,
                                 const QString& kind);

signals:
    // openInBrowser() finished. ok is false when Edge could not be found or started,
    // or when the streaming server would not. Carries no URL, for the reason above.
    void browserOpened(bool ok);

    // found is "" to open the viewer -- a failed read included, since its own decode
    // judges then -- or what the bytes prove the file is instead: "image", "media",
    // "pdf" or "archive".
    void formatChecked(quint64 handle,
                       const QString& name,
                       qulonglong sizeBytes,
                       const QString& kind,
                       const QString& found);

private:
    void onArchiveTailFetched(const QPointer<ArchiveBrowser>& browser,
                              quint64 handle,
                              quint64 sizeBytes,
                              quint64 tailOffset,
                              Result<std::vector<char>> result);
    void onArchiveDirectoryFetched(const QPointer<ArchiveBrowser>& browser,
                                   quint64 handle,
                                   quint64 localHeaderShift,
                                   Result<std::vector<char>> result);

    std::shared_ptr<IMegaClient> mClient;
};
