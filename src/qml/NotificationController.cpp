#include "NotificationController.h"

NotificationController::NotificationController(QObject* parent) : QObject(parent) {}

void NotificationController::notifyError(const QString& context, const QString& errorMessage)
{
    emit errorOccurred(context, errorMessage);
}
