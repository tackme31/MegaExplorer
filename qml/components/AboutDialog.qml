import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml/NewFolderDialog.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import QtQuick.Layouts

// Product identity plus the attributions that have to be visible in the running
// program rather than only in a bundled text file: the MIT copyright notice for
// this app, Qt's own requirement that users be told Qt is used under the LGPL,
// and FFmpeg's requirement of an about-box line. The complete per-component
// texts live one click away in LicenseDialog.qml.
//
// One instance for the whole app, in Main.qml. Unlike NewFolderDialog this can
// use standardButtons -- nothing here needs to keep the dialog open (see
// docs/PROGRESS.md's "standardButtons can't keep a dialog open").
Dialog {
    id: root

    // Main.qml owns both dialogs, so opening the other one is its call, not ours.
    signal licensesRequested

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("About MegaExplorer")
    standardButtons: Dialog.Close
    Component.onCompleted: StandardButtonLabels.pin(footer)

    ColumnLayout {
        spacing: Theme.spacing.md

        Label {
            text: "MegaExplorer"
            font.pixelSize: Theme.font.body + 4
            font.bold: true
        }

        Label {
            text: qsTr("Version %1").arg(Qt.application.version)
            color: Theme.color.textSecondary
        }

        Label {
            text: qsTr("Copyright (c) 2026 Takumi Yamada (tackme31)")
            color: Theme.color.textSecondary
        }

        // The MIT terms in one sentence; the full text is the first entry of
        // the license dialog.
        Label {
            Layout.fillWidth: true
            Layout.maximumWidth: 420
            wrapMode: Text.Wrap
            text: qsTr("This program is free software released under the MIT License, and "
                       + "comes with ABSOLUTELY NO WARRANTY.")
        }

        Label {
            Layout.fillWidth: true
            Layout.maximumWidth: 420
            wrapMode: Text.Wrap
            color: Theme.color.textSecondary
            // Qt's attribution requirement. FFmpeg's wording is prescribed by the
            // project itself (ffmpeg.org/legal.html) -- keep it as-is.
            text: qsTr("Built with the Qt toolkit, used under the GNU Lesser General Public "
                       + "License version 3. This software uses libraries from the FFmpeg "
                       + "project under the LGPLv2.1.")
        }

        Label {
            Layout.fillWidth: true
            Layout.maximumWidth: 420
            wrapMode: Text.Wrap
            color: Theme.color.textSecondary
            textFormat: Text.StyledText
            // Not a MEGA product: say so next to the SDK credit, since the name
            // appears in this app's own.
            text: qsTr("Uses the MEGA C++ SDK. MegaExplorer is an unofficial client and is not "
                       + "affiliated with or endorsed by MEGA. Use of the MEGA service is subject "
                       + "to MEGA's <a href=\"https://mega.io/terms\">Terms of Service</a>.")
            onLinkActivated: link => Qt.openUrlExternally(link)
        }

        Label {
            textFormat: Text.StyledText
            // Named, not bare: LibRaw is linked statically under the LGPL, and
            // this source is what lets a recipient relink against their own
            // build of it (LGPL-2.1 section 6). A naked URL does not say that.
            text: qsTr("Source code: %1").arg(
                      '<a href="https://github.com/tackme31/MegaExplorer">github.com/tackme31/MegaExplorer</a>')
            onLinkActivated: link => Qt.openUrlExternally(link)
        }

        Button {
            Layout.topMargin: Theme.spacing.sm
            text: qsTr("Open source licenses")
            onClicked: root.licensesRequested()
        }
    }
}
