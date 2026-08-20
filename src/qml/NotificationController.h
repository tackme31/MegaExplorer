#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>

#include <QtQml/qqmlregistration.h>

// Shared error-toast relay for controllers with no dedicated UI feedback path.
// Deliberately formats no user-facing text: context says which operation failed and
// ErrorReason says why, so QML composes a localized sentence from the two.
//
// Registered as a QML type purely so QML can name NotificationController.NotFound;
// QML never constructs one.
class NotificationController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided as the notificationController context property")

public:
    // Result::errorCode folded down to what QML can say something useful about. The
    // SDK's own message can't say more -- it is a fixed English table looked up from
    // that same code.
    enum ErrorReason
    {
        NotFound,
        NoPermission,
        Offline,
        Unknown
    };
    Q_ENUM(ErrorReason)

    explicit NotificationController(QObject* parent = nullptr);

    // The code is classified here, once, rather than at each call site; errorMessage
    // reaches QML only when classification gave up, and this is the last route by
    // which an SDK English sentence can reach the UI.
    //
    // Not Q_INVOKABLE: QML only listens to errorOccurred.
    void notifyError(const QString& context, int errorCode, const QString& errorMessage);

    // A failure rejected before any code existed -- this app's own validation, a
    // local settings write. Nothing to classify, so the context alone selects the
    // sentence.
    void notifyError(const QString& context);

    // Outcome of a bulk operation, reported once per user action rather than per
    // item. Same convention: context selects the sentence, the counts fill it in.
    //
    // undo, when non-empty, describes the inverse of what just happened, keyed by
    // "action" -- structure again, not wording. It is carried on this signal rather
    // than a separate one so the toast has it at the moment it is built; nothing
    // stores it, so only the toast still on screen can offer it.
    void notifyOperation(const QString& context,
                         int succeeded,
                         int failed,
                         const QVariantMap& undo = {});

signals:
    // rawMessage is empty unless reason is Unknown.
    void
    errorOccurred(QString context, NotificationController::ErrorReason reason, QString rawMessage);
    void operationFinished(QString context, int succeeded, int failed, QVariantMap undo);

private:
    static ErrorReason classify(int errorCode);
};
