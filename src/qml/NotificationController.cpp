#include "NotificationController.h"

#include "core/MegaErrorCodes.h"

NotificationController::NotificationController(QObject* parent) : QObject(parent) {}

// Allowlist + default, the shape docs/ARCHITECTURE.md's "Collapsing a code to
// a verdict" prescribes: a code the SDK adds later arrives as Unknown, where
// the raw string is still shown, rather than being silently absorbed into one
// of the three sentences below.
NotificationController::ErrorReason NotificationController::classify(int errorCode)
{
    switch (errorCode)
    {
        case MegaErrorCode::kENoEnt:
            return NotFound;
        case MegaErrorCode::kEAccess:
            return NoPermission;
        case MegaErrorCode::kEAgain:
            return Offline;
        default:
            return Unknown;
    }
}

void NotificationController::notifyError(const QString& context,
                                         int errorCode,
                                         const QString& errorMessage)
{
    const ErrorReason reason = classify(errorCode);
    emit errorOccurred(context, reason, reason == Unknown ? errorMessage : QString());
}

void NotificationController::notifyError(const QString& context)
{
    emit errorOccurred(context, Unknown, QString());
}

void NotificationController::notifyOperation(const QString& context, int succeeded, int failed)
{
    emit operationFinished(context, succeeded, failed);
}
