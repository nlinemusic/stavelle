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
//! into Studio's upload window. Jubelle never uploads anything itself.
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
                        text: qsTrc("project", "YouTube account: not connected")
                        font: ui.theme.bodyBoldFont
                        horizontalAlignment: Text.AlignLeft
                    }

                    StyledTextLabel {
                        width: parent.width
                        text: qsTrc("project", "Connecting an account will be added after its Google sign-in screen is approved. You can still publish: YouTube Studio opens in your browser, where you sign in as usual.")
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
                    text: qsTrc("project", "Connect")
                    enabled: false
                    toolTipTitle: qsTrc("project", "Not available yet")
                    toolTipDescription: qsTrc("project", "Waiting for approval of the Google sign-in screen")

                    navigation.name: "Connect"
                    navigation.panel: topPanel
                    navigation.order: 1
                }
            }
        }

        // Jubelle G10: the sign-in design, shown before connecting is switched on
        YouTubeConnectPreview {
            id: connectPreview
            width: parent.width
            visible: false
            onCloseRequested: connectPreview.visible = false
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

        StyledTextLabel {
            width: parent.width
            text: qsTrc("project", "“Open YouTube Studio” opens YouTube’s upload page in your browser and shows the video in your file browser so you can drag it in. Jubelle does not upload anything and never sees your Google password.")
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
