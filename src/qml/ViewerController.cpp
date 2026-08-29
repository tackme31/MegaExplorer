#include "ViewerController.h"

#include "core/PreviewKind.h"

ViewerController::ViewerController(std::shared_ptr<IMegaClient> client, QObject* parent)
    : QObject(parent), mClient(std::move(client))
{}

bool ViewerController::canView(const QString& name) const
{
    return previewKindForName(name.toStdString()) == PreviewKind::Image;
}

QString ViewerController::sourceUrl(quint64 handle)
{
    Result<std::string> url = mClient->streamingUrl(handle);
    if (!url.success)
        return {};
    return QString::fromStdString(url.value());
}
