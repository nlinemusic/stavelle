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

//! Jubelle: choose which part of a picture shows on a project's square card.
//! The picture keeps its proportions: it is scaled to fill the square ("cover")
//! and only its position is chosen. Nothing here touches the score file.
StyledDialogView {
    id: root

    property string projectName: ""
    property string imageUrl: ""
    property real posX: 50
    property real posY: 50
    property bool isNewImage: false

    objectName: "CardImageDialog"

    contentWidth: 760
    contentHeight: content.implicitHeight
    margins: 24

    modal: true

    function done(action) {
        root.ret = { errcode: 0, value: { "action": action, "x": root.posX, "y": root.posY } }
        root.hide()
    }

    function autoCenter() {
        root.posX = 50
        root.posY = 50
    }

    Column {
        id: content

        width: parent.width
        spacing: 18

        StyledTextLabel {
            width: parent.width
            text: qsTrc("project", "Card image for “%1”").arg(root.projectName)
            font: ui.theme.largeBodyBoldFont
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
        }

        RowLayout {
            width: parent.width
            spacing: 28

            Column {
                Layout.alignment: Qt.AlignTop
                spacing: 10

                StyledTextLabel {
                    text: qsTrc("project", "Drag the picture to choose what shows on the card")
                }

                CardImageView {
                    id: cropView

                    width: 340
                    height: 340

                    source: root.imageUrl
                    posX: root.posX
                    posY: root.posY
                    showGuides: true

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor

                        property real startX: 0
                        property real startY: 0
                        property real startPosX: 50
                        property real startPosY: 50

                        onPressed: function(mouse) {
                            startX = mouse.x
                            startY = mouse.y
                            startPosX = root.posX
                            startPosY = root.posY
                        }

                        onPositionChanged: function(mouse) {
                            if (!pressed) {
                                return
                            }
                            var ox = cropView.overflowX
                            var oy = cropView.overflowY
                            if (ox > 0) {
                                root.posX = Math.max(0, Math.min(100, startPosX - (mouse.x - startX) / ox * 100))
                            }
                            if (oy > 0) {
                                root.posY = Math.max(0, Math.min(100, startPosY - (mouse.y - startY) / oy * 100))
                            }
                        }
                    }
                }

                RowLayout {
                    width: 340
                    spacing: 10

                    StyledTextLabel {
                        Layout.fillWidth: true
                        text: root.isNewImage
                              ? qsTrc("project", "A new picture starts centred and scaled to fill the square. It keeps its shape and is never stretched.")
                              : qsTrc("project", "The picture keeps its shape and is never stretched.")
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.Wrap
                        opacity: 0.8
                    }

                    FlatButton {
                        text: qsTrc("project", "Auto Center")
                        icon: IconCode.TEXT_ALIGN_CENTER

                        navigation.panel: NavigationPanel {
                            name: "CardImageCrop"
                            section: root.navigationSection
                            order: 1
                        }

                        onClicked: root.autoCenter()
                    }
                }
            }

            Column {
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: true
                spacing: 10

                StyledTextLabel {
                    text: qsTrc("project", "On the Projects page")
                }

                Rectangle {
                    width: 200
                    height: previewColumn.height
                    radius: 10
                    color: ui.theme.backgroundPrimaryColor
                    border.color: ui.theme.strokeColor
                    border.width: 1
                    clip: true

                    Column {
                        id: previewColumn
                        width: parent.width

                        CardImageView {
                            width: 200
                            height: 200
                            source: root.imageUrl
                            posX: root.posX
                            posY: root.posY
                        }

                        StyledTextLabel {
                            width: parent.width
                            leftPadding: 12
                            topPadding: 10
                            bottomPadding: 10
                            text: root.projectName
                            font: ui.theme.bodyBoldFont
                            horizontalAlignment: Text.AlignLeft
                            elide: Text.ElideRight
                        }
                    }
                }

                NavigationPanel {
                    id: actionsNavPanel
                    name: "CardImageActions"
                    section: root.navigationSection
                    direction: NavigationPanel.Vertical
                    order: 2
                }

                FlatButton {
                    width: 260
                    text: qsTrc("project", "Choose a different image…")
                    icon: IconCode.IMAGE_MOUNTAINS
                    orientation: Qt.Horizontal

                    navigation.panel: actionsNavPanel
                    navigation.row: 1

                    onClicked: root.done("choose")
                }

                FlatButton {
                    width: 260
                    visible: !root.isNewImage
                    text: qsTrc("project", "Remove image (use Jubelle default)")

                    navigation.panel: actionsNavPanel
                    navigation.row: 2

                    onClicked: root.done("remove")
                }

                StyledTextLabel {
                    width: parent.width
                    text: qsTrc("project", "JPG, PNG or WebP. A new picture replaces the current one; Cancel keeps the current one unchanged.")
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.Wrap
                    opacity: 0.7
                }
            }
        }

        StyledTextLabel {
            width: parent.width
            text: qsTrc("project", "This picture only appears on the Projects page. It is kept separately from the score: it is never added to the score, never changes its layout, and never appears in PDF, audio or video exports. Your score file is not changed.")
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            opacity: 0.8
        }

        ButtonBox {
            width: parent.width

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 3

            FlatButton {
                text: qsTrc("global", "Cancel")
                buttonRole: ButtonBoxModel.RejectRole
                buttonId: ButtonBoxModel.Cancel

                onClicked: root.reject()
            }

            FlatButton {
                text: qsTrc("global", "Save")
                accentButton: true
                buttonRole: ButtonBoxModel.AcceptRole
                buttonId: ButtonBoxModel.Ok

                onClicked: root.done("save")
            }
        }
    }
}
