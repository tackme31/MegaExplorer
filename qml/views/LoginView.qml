import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml/FileTableView.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import QtQuick.Layouts

// Shown by Main.qml's Loader instead of the header/footer/content whenever
// authController.authState !== AuthController.LoggedIn. All user-facing text
// is composed here from authController's structured authErrorKind/
// rawErrorMessage -- same "C++ passes fields, QML composes text" convention
// as NotificationController/ToastStack.qml. describeError() varies its
// wording by the current state too, since e.g. InvalidCredentials reads
// differently on the password step vs. the 2FA code step.
Item {
    id: root

    readonly property bool isTwoFactorStep: authController.authState
                                            === AuthController.NeedsTwoFactor
                                            || authController.authState
                                            === AuthController.VerifyingTwoFactor
    readonly property bool restoring: authController.authState === AuthController.Restoring

    function describeError(kind, state) {
        switch (kind) {
        case AuthController.InvalidCredentials:
            return state === AuthController.NeedsTwoFactor || state
                    === AuthController.VerifyingTwoFactor ? qsTr(
                                                                "The code you entered is incorrect.") :
                                                            qsTr("Incorrect email or password.");
        case AuthController.AccountBlocked:
            return qsTr("This account has been suspended.");
        case AuthController.TooManyAttempts:
            return qsTr("Too many attempts. Please try again later.");
        case AuthController.NetworkError:
            return qsTr("Couldn't connect. Please check your connection.");
        case AuthController.UnknownError:
            return authController.rawErrorMessage;
        default:
            return "";
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(320, root.width - 48)
        spacing: 12

        Label {
            Layout.alignment: Qt.AlignHCenter
            visible: root.restoring
            text: qsTr("Signing you in…")
        }

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            visible: root.restoring
            running: root.restoring
        }

        ColumnLayout {
            visible: !root.restoring && !root.isTwoFactorStep
            spacing: 8

            TextField {
                id: emailField
                Layout.fillWidth: true
                enabled: authController.authState === AuthController.LoggedOut
                placeholderText: qsTr("Email")
            }

            TextField {
                id: passwordField
                Layout.fillWidth: true
                enabled: authController.authState === AuthController.LoggedOut
                placeholderText: qsTr("Password")
                echoMode: TextInput.Password
                onAccepted: signInButton.clicked()
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "crimson"
                visible: authController.authErrorKind !== AuthController.NoError
                text: root.describeError(authController.authErrorKind, authController.authState)
            }

            Button {
                id: signInButton
                Layout.fillWidth: true
                enabled: authController.authState === AuthController.LoggedOut
                text: qsTr("Sign in")
                onClicked: authController.login(emailField.text, passwordField.text)
            }
        }

        ColumnLayout {
            visible: root.isTwoFactorStep
            spacing: 8

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Enter the 6-digit code from your authenticator app")
            }

            TextField {
                id: codeField
                Layout.fillWidth: true
                horizontalAlignment: TextInput.AlignHCenter
                enabled: authController.authState === AuthController.NeedsTwoFactor
                inputMethodHints: Qt.ImhDigitsOnly
                maximumLength: 6
                onAccepted: confirmButton.clicked()
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "crimson"
                visible: authController.authErrorKind !== AuthController.NoError
                text: root.describeError(authController.authErrorKind, authController.authState)
            }

            RowLayout {
                Layout.fillWidth: true

                Button {
                    Layout.fillWidth: true
                    text: qsTr("Back")
                    onClicked: authController.cancelTwoFactor()
                }

                Button {
                    id: confirmButton
                    Layout.fillWidth: true
                    enabled: authController.authState === AuthController.NeedsTwoFactor
                    text: qsTr("Confirm")
                    onClicked: authController.submitTwoFactorCode(codeField.text)
                }
            }
        }
    }
}
