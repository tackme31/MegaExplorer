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

signals:
    void errorOccurred(QString context, QString errorMessage);
};
