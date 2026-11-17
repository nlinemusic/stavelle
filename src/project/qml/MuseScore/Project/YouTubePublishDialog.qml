/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 * Music Composition & Notation
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

//! Jubelle: Publish to YouTube. Pick an exported video, then open YouTube Studio
//! in the web browser; the video is shown in Explorer/Finder so it can be dragged
//! into Studio's upload window; or, once connected (design approved 17 Nov 2026), upload from here.
StyledDialogView {
    id: root

    title: qsTrc("project", "Publish to YouTube")
    objectName: "YouTubePublishDialog"

    contentWidth: 560
    contentHeight: content.implicitHeight
    margins: 24

    modal: true

    YouTubePublishModel {
        id: ytModel
    }

    Component.onCompleted: ytModel.load()

    Column {
        id: content

        width: parent.width
        spacing: 18

        // Connection status
        Rectangle {
            width: parent.width
            height: statusRow.implicitHeight + 24
            radius: 8
            color: ui.theme.backgroundSecondaryColor
            border.color: ui.theme.strokeColor
            border.width: 1

            RowLayout {
                id: statusRow

                anchors.fill: parent
                anchors.margins: 12
                spacing: 12

                StyledIconLabel {
                    iconCode: IconCode.VIDEO
                    font.pixelSize: 24
                    color: "#E600A9"
                }

                Column {
                    Layout.fillWidth: true
                    spacing: 2

                    StyledTextLabel {
                        width: parent.width
                        text: ytModel.isConnected ? qsTrc("project", "YouTube account: connected")
                                                  : qsTrc("project", "YouTube account: not connected")
                        font: ui.theme.bodyBoldFont
                        horizontalAlignment: Text.AlignLeft
                    }

                    StyledTextLabel {
                        width: parent.width
                        text: ytModel.isConnected
                              ? qsTrc("project", "Jubelle can upload videos to your channel. It cannot delete videos or change your channel.")
                              : qsTrc("project", "Connect once to upload from here, or use YouTube Studio in your browser.")
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.Wrap
                        opacity: 0.75
                    }
                }

                FlatButton {
                    text: qsTrc("project", "How connecting works")
                    onClicked: connectPreview.visible = !connectPreview.visible
                }

                FlatButton {
                    visible: !ytModel.isConnected
                    text: qsTrc("project", "Connect")
                    enabled: ytModel.hasClient && !ytModel.busy
                    toolTipTitle: ytModel.hasClient ? "" : qsTrc("project", "Set up Google access first")
                    navigation.name: "Connect"
                    navigation.panel: topPanel
                    navigation.order: 1
                    onClicked: ytModel.connectAccount()
                }

                FlatButton {
                    visible: ytModel.isConnected
                    text: qsTrc("project", "Disconnect")
                    enabled: !ytModel.busy
                    navigation.name: "Disconnect"
                    navigation.panel: topPanel
                    navigation.order: 2
                    onClicked: ytModel.disconnectAccount()
                }
            }
        }

        YouTubeConnectPreview {
            id: connectPreview
            width: parent.width
            visible: !ytModel.hasClient && !ytModel.isConnected
            canSignIn: ytModel.hasClient && !ytModel.busy
            connected: ytModel.isConnected
            onCloseRequested: connectPreview.visible = false
            onSignInRequested: ytModel.connectAccount()
        }

        // One-time set-up: your own Google client (Jubelle has no shared one)
        Rectangle {
            width: parent.width
            height: setupColumn.implicitHeight + 24
            radius: 8
            color: ui.theme.backgroundSecondaryColor
            visible: !ytModel.isConnected && (connectPreview.visible || !ytModel.hasClient)

            Column {
                id: setupColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 12
                spacing: 8

                StyledTextLabel {
                    width: parent.width
                    horizontalAlignment: Text.AlignLeft
                    font: ui.theme.bodyBoldFont
                    text: qsTrc("project", "Google access (one-time set-up)")
                }
                StyledTextLabel {
                    width: parent.width
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.Wrap
                    textFormat: Text.StyledText
                    opacity: 0.85
                    text: qsTrc("project", "In Google Cloud Console: create a project, switch on <b>YouTube Data API v3</b>, set up the consent screen (add yourself as a test user), then create an OAuth client of type <b>Desktop app</b>. Paste its client ID and secret here. They stay on this computer.")
                }
                FlatButton {
                    text: qsTrc("project", "Open Google Cloud Console")
                    onClicked: ytModel.openGoogleSetupPage()
                }
                TextInputField {
                    id: clientIdField
                    width: parent.width
                    hint: qsTrc("project", "Client ID (…apps.googleusercontent.com)")
                    currentText: ytModel.clientId
                    onTextChanged: function(newText) { clientIdField.currentText = newText }
                }
                TextInputField {
                    id: clientSecretField
                    width: parent.width
                    hint: ytModel.hasClient ? qsTrc("project", "Client secret (saved; type to replace)") : qsTrc("project", "Client secret")
                    onTextChanged: function(newText) { clientSecretField.currentText = newText }
                }
                FlatButton {
                    text: qsTrc("project", "Save")
                    enabled: clientIdField.currentText !== "" && (ytModel.hasClient || clientSecretField.currentText !== "")
                    onClicked: {
                        ytModel.saveClient(clientIdField.currentText, clientSecretField.currentText)
                        clientSecretField.currentText = ""
                    }
                }
            }
        }

        NavigationPanel {
            id: topPanel
            name: "YouTubeStatus"
            section: root.navigationSection
            order: 1
        }

        StyledTextLabel {
            width: parent.width
            text: ytModel.videos.length > 0
                  ? qsTrc("project", "Choose the video to publish (newest first):")
                  : qsTrc("project", "No exported videos were found in this project’s Video Exports folder. Export a video first, or choose a video file yourself.")
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
        }

        NavigationPanel {
            id: listPanel
            name: "YouTubeVideos"
            section: root.navigationSection
            direction: NavigationPanel.Vertical
            order: 2
        }

        Column {
            width: parent.width
            spacing: 6
            visible: ytModel.videos.length > 0

            Repeater {
                model: ytModel.videos

                delegate: RoundedRadioButton {
                    required property var modelData
                    required property int index

                    width: parent.width
                    text: qsTrc("project", "%1 — exported %2").arg(modelData.name).arg(modelData.exported)
                    checked: modelData.path === ytModel.selectedPath

                    navigation.name: modelData.name
                    navigation.panel: listPanel
                    navigation.row: index

                    onToggled: ytModel.selectedPath = modelData.path
                }
            }
        }

        StyledTextLabel {
            width: parent.width
            visible: ytModel.selectedPath !== "" && !videoIsListed()
            text: qsTrc("project", "Chosen file: %1").arg(ytModel.selectedPath)
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WrapAnywhere
            opacity: 0.85

            function videoIsListed() {
                for (var i = 0; i < ytModel.videos.length; ++i) {
                    if (ytModel.videos[i].path === ytModel.selectedPath) {
                        return true
                    }
                }
                return false
            }
        }

        Flow {
            width: parent.width
            spacing: 8

            NavigationPanel {
                id: actionsPanel
                name: "YouTubeFileActions"
                section: root.navigationSection
                order: 3
            }

            FlatButton {
                text: qsTrc("project", "Show in folder")
                enabled: ytModel.selectedPath !== ""
                navigation.panel: actionsPanel
                navigation.order: 1
                onClicked: ytModel.revealSelected()
            }

            FlatButton {
                text: qsTrc("project", "Choose another file…")
                navigation.panel: actionsPanel
                navigation.order: 2
                onClicked: ytModel.chooseOtherFile()
            }

            FlatButton {
                text: qsTrc("project", "Export a new video…")
                navigation.panel: actionsPanel
                navigation.order: 3
                onClicked: {
                    root.reject()
                    ytModel.exportNewVideo()
                }
            }
        }

        // Upload from Jubelle (when connected)
        Column {
            width: parent.width
            spacing: 8
            visible: ytModel.isConnected

            TextInputField {
                id: titleField
                width: parent.width
                hint: qsTrc("project", "Title (default: the project's name)")
                currentText: ytModel.projectName
                onTextChanged: function(newText) { titleField.currentText = newText }
            }
            TextInputField {
                id: descriptionField
                width: parent.width
                hint: qsTrc("project", "Description (optional)")
                onTextChanged: function(newText) { descriptionField.currentText = newText }
            }
            Row {
                spacing: 8
                StyledTextLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTrc("project", "Who can see it:")
                }
                StyledDropdown {
                    id: privacyBox
                    width: 200
                    model: [
                        { value: 0, text: qsTrc("project", "Private (only you)") },
                        { value: 1, text: qsTrc("project", "Unlisted (anyone with the link)") },
                        { value: 2, text: qsTrc("project", "Public") }
                    ]
                    currentIndex: 0
                    onActivated: function(i, v) { privacyBox.currentIndex = i }
                }
                FlatButton {
                    accentButton: true
                    text: qsTrc("project", "Upload to YouTube")
                    enabled: ytModel.selectedPath !== "" && !ytModel.busy
                    onClicked: ytModel.upload(titleField.currentText, descriptionField.currentText, privacyBox.currentIndex)
                }
                FlatButton {
                    visible: ytModel.busy && ytModel.uploadPercent >= 0
                    text: qsTrc("global", "Cancel")
                    onClicked: ytModel.cancelUpload()
                }
            }
        }

        ProgressBar {
            width: parent.width
            visible: ytModel.uploadPercent >= 0
            from: 0
            to: 100
            value: Math.max(0, ytModel.uploadPercent)
        }

        StyledTextLabel {
            width: parent.width
            visible: ytModel.statusText !== ""
            text: ytModel.statusText
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
        }

        FlatButton {
            visible: ytModel.lastLink !== ""
            text: qsTrc("project", "Open the video on YouTube")
            onClicked: ytModel.openLastLink()
        }

        StyledTextLabel {
            width: parent.width
            text: qsTrc("project", "“Open YouTube Studio” opens YouTube’s upload page in your browser and shows the video in your file browser so you can drag it in. Either way, Jubelle never sees your Google password.")
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            opacity: 0.75
        }

        ButtonBox {
            width: parent.width

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 4

            FlatButton {
                text: qsTrc("global", "Close")
                buttonRole: ButtonBoxModel.RejectRole
                buttonId: ButtonBoxModel.Cancel

                onClicked: root.reject()
            }

            FlatButton {
                text: qsTrc("project", "Open YouTube Studio")
                accentButton: true
                enabled: ytModel.selectedPath !== ""
                buttonRole: ButtonBoxModel.AcceptRole
                buttonId: ButtonBoxModel.Ok

                onClicked: {
                    ytModel.openYouTubeStudio()
                    root.hide()
                }
            }
        }
    }
}
