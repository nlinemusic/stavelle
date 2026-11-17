/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

//! Jubelle G10: what connecting a YouTube account will look like, shown for approval.
//! The Connect button stays switched off until the sign-in design is approved.
Rectangle {
    id: root

    signal closeRequested()

    readonly property color jubelleMagenta: "#E600A9"

    implicitHeight: column.implicitHeight + 32
    radius: 8
    color: ui.theme.backgroundSecondaryColor
    border.color: root.jubelleMagenta
    border.width: 1

    component Step: RowLayout {
        property string number
        property string heading
        property string detail
        Layout.fillWidth: true
        spacing: 12
        Rectangle {
            Layout.alignment: Qt.AlignTop
            width: 26
            height: 26
            radius: 13
            color: root.jubelleMagenta
            StyledTextLabel {
                anchors.centerIn: parent
                text: number
                color: "white"
                font: ui.theme.bodyBoldFont
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                font: ui.theme.bodyBoldFont
                text: heading
            }
            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.Wrap
                opacity: 0.8
                text: detail
            }
        }
    }

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        spacing: 14

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            font: ui.theme.largeBodyBoldFont
            text: qsTrc("project", "Connect your YouTube account")
        }

        Step {
            number: "1"
            heading: qsTrc("project", "You sign in on Google's own page")
            detail: qsTrc("project", "Jubelle opens Google's sign-in page in your web browser. You sign in there, so Jubelle never sees your Google password.")
        }
        Step {
            number: "2"
            heading: qsTrc("project", "Google asks for one permission")
            detail: qsTrc("project", "“Manage your YouTube videos”: Jubelle may upload videos to your channel. Jubelle does not delete videos, read comments or messages, or change your channel settings.")
        }
        Step {
            number: "3"
            heading: qsTrc("project", "Back in Jubelle: connected")
            detail: qsTrc("project", "This window shows your channel's name. From then on, Publish uploads the chosen video: you give the title and description, it is Private unless you choose otherwise, and you see the progress and a link when it is done.")
        }

        SeparatorLine { Layout.fillWidth: true }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            textFormat: Text.StyledText
            text: qsTrc("project", "<b>What Jubelle keeps:</b> a sign-in token, on this computer only (in Windows Credential Manager), never in your scores or projects. It is sent only to Google.<br/><b>Disconnecting:</b> one button here removes the token. You can also remove Jubelle's access at any time in your Google account under Security › Third-party access.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.Wrap
                opacity: 0.7
                text: qsTrc("project", "Preview only: connecting is switched off until you approve this sign-in design.")
            }
            FlatButton {
                text: qsTrc("global", "Close")
                onClicked: root.closeRequested()
            }
            FlatButton {
                accentButton: true
                enabled: false
                text: qsTrc("project", "Sign in with Google")
                toolTipTitle: qsTrc("project", "Not switched on yet")
                toolTipDescription: qsTrc("project", "Waiting for your approval of this sign-in design")
            }
        }
    }
}
