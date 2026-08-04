#pragma once
#include "core/AuthService.h"

#include <QObject>
#include <QString>

#include <memory>
#include <QtQml/qqmlregistration.h>
#include <string>

// QML-facing GUI glue wrapping AuthService, registered as the "authController"
// context property (main.cpp). Also registered as a QML type
// (QML_ELEMENT/QML_UNCREATABLE) -- this codebase's first -- purely so QML can
// reference AuthController.LoggedIn etc. by type name; main.cpp still hands
// QML the single instance via setContextProperty, QML never constructs one
// itself.
//
// Deliberately has no NotificationController* dependency, unlike every other
// controller in this directory: login/2FA failures are shown inline on the
// login screen (via authErrorKind/rawErrorMessage below), a restore failure
// is either silent or shown inline on that same screen, and logout always
// succeeds from the caller's perspective (AuthService::logout's own
// contract) -- there is no failure path left that needs a global toast.
class AuthController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided as the authController context property")

public:
    enum AuthState
    {
        Restoring,
        LoggedOut,
        LoggingIn,
        NeedsTwoFactor,
        VerifyingTwoFactor,
        LoggedIn,
        LoggingOut
    };
    Q_ENUM(AuthState)

    enum AuthErrorKind
    {
        NoError,
        InvalidCredentials,
        AccountBlocked,
        TooManyAttempts,
        NetworkError,
        UnknownError
    };
    Q_ENUM(AuthErrorKind)

    Q_PROPERTY(AuthState authState READ authState NOTIFY authStateChanged)
    Q_PROPERTY(AuthErrorKind authErrorKind READ authErrorKind NOTIFY authErrorKindChanged)
    // Only ever populated for UnknownError -- every other AuthErrorKind maps
    // to a fixed, localized sentence composed entirely in QML (see
    // LoginView.qml's describeError()), same "C++ passes structured fields,
    // QML composes text" convention as NotificationController/ToastStack.qml.
    Q_PROPERTY(QString rawErrorMessage READ rawErrorMessage NOTIFY authErrorKindChanged)

    explicit AuthController(std::shared_ptr<AuthService> authService, QObject* parent = nullptr);

    // Not Q_INVOKABLE: called once from main.cpp's composition root before
    // app.exec(), same convention as FolderNavigationController::loadRoot.
    void restoreSession();

    Q_INVOKABLE void login(const QString& email, const QString& password);
    Q_INVOKABLE void submitTwoFactorCode(const QString& pin);
    Q_INVOKABLE void cancelTwoFactor();
    Q_INVOKABLE void logout();

    AuthState authState() const;
    AuthErrorKind authErrorKind() const;
    QString rawErrorMessage() const;

signals:
    void authStateChanged();
    void authErrorKindChanged();

private:
    void setState(AuthState state);
    void setError(AuthErrorKind kind, const QString& rawMessage = QString());
    AuthErrorKind classifyError(int errorCode) const;

    std::shared_ptr<AuthService> mAuthService;
    AuthState mState = Restoring;
    AuthErrorKind mErrorKind = NoError;
    QString mRawErrorMessage;
    // Held between login()'s kEMfaRequired failure and the matching
    // submitTwoFactorCode()/cancelTwoFactor() -- cleared on either path.
    std::string mPendingEmail;
    std::string mPendingPassword;
};
