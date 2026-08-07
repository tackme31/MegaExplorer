#pragma once
#include <QObject>
#include <QString>

#include <QtQml/qqmlregistration.h>

// Shared, generic error-toast relay for controllers with no dedicated UI
// feedback path (FolderNavigationController, ThumbnailController,
// DownloadController's openFile sub-path). Deliberately doesn't format any
// user-facing text itself -- context identifies which operation failed
// (e.g. "navigation", "search", "thumbnail", "openFile") and ErrorReason says
// why, so ToastStack.qml's showError() can compose a localized sentence out of
// the two, same as showDownload composes its own text from structured fields
// rather than receiving a pre-formatted string.
//
// Registered as its own "notificationController" context property (main.cpp)
// and injected into the other controllers' constructors as a non-owning
// pointer. Also registered as a QML type purely so QML can name
// NotificationController.NotFound etc., the same reason AuthController is
// (see its header) -- QML never constructs one.
class NotificationController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided as the notificationController context property")

public:
    // Why an operation failed, folded down from Result::errorCode. Four values
    // because that is what QML can say something useful about; the SDK's own
    // message can't say more, since it is a fixed English table looked up from
    // that same code (see docs/ARCHITECTURE.md, "Error representation").
    enum ErrorReason
    {
        NotFound,
        NoPermission,
        Offline,
        Unknown
    };
    Q_ENUM(ErrorReason)

    explicit NotificationController(QObject* parent = nullptr);

    // A failure that carries an SDK/service errorCode. The code is classified
    // here, once, rather than at each call site; errorMessage reaches QML only
    // when classification gave up. Since R5-10 dropped the raw text from the
    // login screen and the download snackbar, this is the last route by which
    // an SDK English sentence can reach the UI.
    //
    // Not Q_INVOKABLE: only C++ controllers call this; QML only ever listens
    // to errorOccurred.
    void notifyError(const QString& context, int errorCode, const QString& errorMessage);

    // A failure rejected before any code existed -- a name this app's own
    // validation refused, a local settings write, a pre-flight check. There is
    // no reason to classify and no string worth showing, so the context alone
    // has to select a fixed sentence.
    void notifyError(const QString& context);

    // Outcome of a bulk operation that fanned out over N selected items (see
    // FileMutationController::moveHandlesToRubbish). Same structured-
    // fields convention as notifyError: context selects the sentence,
    // succeeded/failed are the numbers QML plugs into it. Reported once per
    // user action, not once per item. Not Q_INVOKABLE, same as notifyError.
    void notifyOperation(const QString& context, int succeeded, int failed);

signals:
    // rawMessage is empty unless reason is Unknown.
    void
    errorOccurred(QString context, NotificationController::ErrorReason reason, QString rawMessage);
    void operationFinished(QString context, int succeeded, int failed);

private:
    static ErrorReason classify(int errorCode);
};
