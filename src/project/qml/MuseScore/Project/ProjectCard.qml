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

//! Jubelle: a project card. The picture area is an exact square; the name, last
//! saved time, tags and "Also in …" sit below it. Without a picture the Jubelle
//! default tile is shown (charcoal, faint purple staff lines, magenta initial).
Item {
    id: root

    property var item: ({})
    property bool compact: false
    property bool showMenu: false
    property string revealTitle: ""

    property alias navigation: navCtrl

    signal openRequested()
    signal menuItemTriggered(string itemId)

    implicitHeight: card.height

    NavigationControl {
        id: navCtrl
        name: "ProjectCard " + (root.item.name || "")
        enabled: root.enabled && root.visible

        accessible.role: MUAccessible.Button
        accessible.name: root.item.name || ""

        onTriggered: root.openRequested()
    }

    Rectangle {
        id: card

        width: parent.width
        height: column.height
        radius: root.compact ? 8 : 12
        color: ui.theme.backgroundSecondaryColor
        border.color: navCtrl.highlight ? ui.theme.fontPrimaryColor
                                        : (mouseArea.containsMouse ? ui.theme.accentColor : ui.theme.strokeColor)
        border.width: navCtrl.highlight ? ui.theme.navCtrlBorderWidth : 1
        clip: true

        Column {
            id: column
            width: parent.width

            Item {
                width: parent.width
                height: width

                CardImageView {
                    anchors.fill: parent
                    visible: Boolean(root.item.hasImage)
                    source: root.item.hasImage ? root.item.imageUrl : ""
                    posX: root.item.imageX !== undefined ? root.item.imageX : 50
                    posY: root.item.imageY !== undefined ? root.item.imageY : 50
                }

                Rectangle {
                    anchors.fill: parent
                    visible: !root.item.hasImage
                    color: "#1A1A23"

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        spacing: parent.height * 0.04

                        Repeater {
                            model: 5
                            Rectangle {
                                width: parent.width
                                height: 1
                                color: "#A855F7"
                                opacity: 0.22
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: root.item.initial || ""
                        color: "#E600A9"
                        font.pixelSize: parent.height * 0.33
                        font.bold: true
                    }
                }
            }

            Column {
                width: parent.width
                padding: root.compact ? 10 : 12
                spacing: 6

                RowLayout {
                    width: parent.width - 2 * parent.padding
                    spacing: 6

                    Column {
                        Layout.fillWidth: true
                        spacing: 2

                        StyledTextLabel {
                            width: parent.width
                            text: root.item.name || ""
                            font: ui.theme.bodyBoldFont
                            horizontalAlignment: Text.AlignLeft
                            elide: Text.ElideRight
                        }

                        StyledTextLabel {
                            width: parent.width
                            visible: Boolean(root.item.saved)
                            text: qsTrc("project", "Saved %1").arg(root.item.saved || "")
                            horizontalAlignment: Text.AlignLeft
                            elide: Text.ElideRight
                            opacity: 0.7
                        }
                    }

                    MenuButton {
                        id: menuButton

                        Layout.alignment: Qt.AlignTop
                        visible: root.showMenu

                        navigation.name: "ProjectMenu " + (root.item.name || "")
                        navigation.panel: navCtrl.panel
                        navigation.row: navCtrl.row
                        navigation.column: navCtrl.column + 10000

                        menuModel: {
                            var result = [
                                { id: "open", title: qsTrc("project", "Open") },
                                { id: "rename", title: qsTrc("project", "Rename…") },
                                { id: "move", title: qsTrc("project", "Move to category…") },
                                { id: "categories-tags", title: qsTrc("project", "Categories and tags…") },
                                {},
                                { id: "card-image", title: root.item.hasImage ? qsTrc("project", "Change card image…")
                                                                              : qsTrc("project", "Add card image…") }
                            ]
                            if (root.item.hasImage) {
                                result.push({ id: "remove-card-image", title: qsTrc("project", "Remove card image") })
                            }
                            result.push({})
                            result.push({ id: "pictures", title: qsTrc("project", "Open Pictures folder") })
                            result.push({ id: "reveal", title: root.revealTitle })
                            return result
                        }

                        onHandleMenuItem: function(itemId) {
                            root.menuItemTriggered(itemId)
                        }
                    }
                }

                Flow {
                    width: parent.width - 2 * parent.padding
                    spacing: 6
                    visible: !root.compact && root.item.tags !== undefined && root.item.tags.length > 0

                    Repeater {
                        model: root.compact ? [] : (root.item.tags || [])

                        delegate: Rectangle {
                            required property var modelData

                            width: tagLabel.implicitWidth + 16
                            height: tagLabel.implicitHeight + 6
                            radius: height / 2
                            color: ui.theme.buttonColor

                            StyledTextLabel {
                                id: tagLabel
                                anchors.centerIn: parent
                                text: modelData
                            }
                        }
                    }
                }

                StyledTextLabel {
                    width: parent.width - 2 * parent.padding
                    visible: !root.compact && Boolean(root.item.otherCategories)
                    text: qsTrc("project", "Also in %1").arg(root.item.otherCategories || "")
                    color: "#C9A2FA"
                    horizontalAlignment: Text.AlignLeft
                    elide: Text.ElideRight
                }
            }
        }

        MouseArea {
            id: mouseArea

            // Below the menu button, so the ⋯ button still receives its own clicks.
            z: -1
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton

            onClicked: root.openRequested()
        }
    }
}
