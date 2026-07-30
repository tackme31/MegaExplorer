#include "NotificationController.h"

NotificationController::NotificationController(QObject* parent) : QObject(parent) {}

void NotificationController::notifyError(const QString& context, const QString& errorMessage)
{
    emit errorOccurred(context, errorMessage);
}

void NotificationController::notifyOperation(const QString& context, int succeeded, int failed)
{
    emit operationFinished(context, succeeded, failed);
}
