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
// loadingStage -- same "C++ passes fields, QML composes text" convention as
// NotificationController/ToastStack.qml. describeError()
// varies its wording by the current state too, since e.g. InvalidCredentials
// reads differently on the password step vs. the 2FA code step.
//
// The three pages are a StackLayout rather than visible-toggled siblings so
// the panel keeps one height across state changes (S11, docs/
// DESIGN_IMPROVEMENT.md section 8): a StackLayout's implicit size is the
// maximum over all its children, not just the current one. StackLayout drives
// its children's visible property itself, so no page carries a visible:
// binding of its own -- they would fight.
Item {
    id: root

    readonly property int currentPage: {
        switch (authController.authState) {
        case AuthController.LoggedOut:
            return 0;
        case AuthController.NeedsTwoFactor:
            return 1;
        default:
            // Restoring / LoggingIn / VerifyingTwoFactor / LoggingOut. Every
            // one of them is a wait with nothing for the user to do, and on a
            // large account the wait after a fresh sign-in runs to minutes.
            return 2;
        }
    }

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
            // Not a rare fallback: since R3-1 every failure the SDK layer couldn't
            // classify arrives as kEInternal, so this is the wording most unusual
            // login failures get. The SDK's own sentence is English-only and stays
            // in AuthController's qCWarning (R5-10).
        case AuthController.UnknownError:
            return qsTr("Couldn't sign in. Please try again.");
        default:
            return "";
        }
    }

    function describeStage(stage) {
        switch (stage) {
        case AuthController.DownloadingNodes:
            // Deliberately says "file list", not "loading": this bar covers
            // only the download, which is under half of the total wait. A bar
            // that says "loading" and then sits at 100% for another few
            // minutes is the exact problem this screen exists to fix.
            return qsTr("Downloading your file list…");
        case AuthController.DecryptingNodes:
            return qsTr("Decrypting your file list…");
        case AuthController.SigningOut:
            return qsTr("Signing you out…");
        default:
            return qsTr("Signing you in…");
        }
    }

    StackLayout {
        anchors.centerIn: parent
        width: Math.min(320, root.width - 48)
        currentIndex: root.currentPage

        // Each page pads itself with fillHeight spacers because StackLayout
        // stretches every child to the layout's full height (its children
        // default Layout.fillHeight to true), and the layout is as tall as
        // the tallest page.
        ColumnLayout {
            spacing: 8

            Item {
                Layout.fillHeight: true
            }

            TextField {
                id: emailField
                Layout.fillWidth: true
                placeholderText: qsTr("Email")
            }

            TextField {
                id: passwordField
                Layout.fillWidth: true
                placeholderText: qsTr("Password")
                echoMode: TextInput.Password
                onAccepted: signInButton.clicked()
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.color.danger
                visible: authController.authErrorKind !== AuthController.NoError
                text: root.describeError(authController.authErrorKind, authController.authState)
            }

            Button {
                id: signInButton
                Layout.fillWidth: true
                text: qsTr("Sign in")
                onClicked: authController.login(emailField.text, passwordField.text)
            }

            Item {
                Layout.fillHeight: true
            }
        }

        ColumnLayout {
            spacing: 8

            Item {
                Layout.fillHeight: true
            }

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
                inputMethodHints: Qt.ImhDigitsOnly
                maximumLength: 6
                onAccepted: confirmButton.clicked()
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.color.danger
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
                    text: qsTr("Confirm")
                    onClicked: authController.submitTwoFactorCode(codeField.text)
                }
            }

            Item {
                Layout.fillHeight: true
            }
        }

        ColumnLayout {
            id: loadingPage
            spacing: 8

            readonly property bool downloading: authController.loadingStage
                                                === AuthController.DownloadingNodes
            readonly property bool decrypting: authController.loadingStage
                                               === AuthController.DecryptingNodes

            Item {
                Layout.fillHeight: true
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                text: root.describeStage(authController.loadingStage)
            }

            ProgressBar {
                Layout.fillWidth: true
                visible: loadingPage.downloading
                from: 0
                to: 1
                value: authController.fetchProgress
            }

            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                visible: !loadingPage.downloading
                // Gated on isCurrentItem as well as visible: whether the
                // style stops animating a hidden indicator is style-private,
                // and LoginView is never destroyed while logged out, so a
                // stuck animation would drive the render loop for the whole
                // session.
                running: visible && loadingPage.StackLayout.isCurrentItem
            }

            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Theme.font.caption
                color: Theme.color.textSecondary
                visible: loadingPage.downloading
                text: authController.fetchProgressText
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Theme.font.caption
                color: Theme.color.textSecondary
                visible: loadingPage.decrypting
                // Says nothing about later sign-ins being fast: that holds
                // only while the SDK's state-cache DB survives, and signing
                // out destroys it.
                text: qsTr("This can take a few minutes the first time you sign in.")
            }

            Item {
                Layout.fillHeight: true
            }
        }
    }
}
