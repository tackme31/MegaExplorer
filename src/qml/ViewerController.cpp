#include "ViewerController.h"

#include "core/PreviewKind.h"

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
        // The rest never get one.
        case PreviewKind::Text:
        case PreviewKind::Archive:
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
