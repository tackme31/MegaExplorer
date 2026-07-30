#pragma once
#include <QObject>
#include <QString>

// Shared, generic error-toast relay for controllers with no dedicated UI
// feedback path (FolderNavigationController, ThumbnailController,
// DownloadController's openFile sub-path). Deliberately doesn't format any
// user-facing text itself -- context identifies which operation failed
// (e.g. "navigation", "search", "thumbnail", "openFile") so ErrorToast.qml
// can compose a localized sentence per context, same as DownloadSnackbar
// composes its own text from structured fields rather than receiving a
// pre-formatted string. Registered as its own "notificationController"
// context property (main.cpp) and injected into the other controllers'
// constructors as a non-owning pointer.
class NotificationController : public QObject
{
    Q_OBJECT

public:
    explicit NotificationController(QObject* parent = nullptr);

    // Not Q_INVOKABLE: only C++ controllers call this; QML only ever listens
    // to errorOccurred.
    void notifyError(const QString& context, const QString& errorMessage);

    // Outcome of a bulk operation that fanned out over N selected items (see
    // FolderNavigationController::moveSelectionToRubbish). Same structured-
    // fields convention as notifyError: context selects the sentence,
    // succeeded/failed are the numbers QML plugs into it. Reported once per
    // user action, not once per item. Not Q_INVOKABLE, same as notifyError.
    void notifyOperation(const QString& context, int succeeded, int failed);

signals:
    void errorOccurred(QString context, QString errorMessage);
    void operationFinished(QString context, int succeeded, int failed);
};
