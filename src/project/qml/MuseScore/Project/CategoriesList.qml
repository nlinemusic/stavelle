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

//! Jubelle: the category column of the Projects page. Click to select, drag the
//! handle to reorder, right-click for Rename / Move up / Move down /
//! Show in folder / Delete.
Column {
    id: root

    property var pageModel: null
    property NavigationSection navigationSection: null
    property string revealTitle: ""

    padding: 14
    spacing: 4

    readonly property real innerWidth: width - 2 * padding

    property int dragSourceIndex: -1
    property int dropTargetIndex: -1

    //! The category row under the given y (in this item's coordinates), or -1.
    function indexAt(y) {
        for (var i = 0; i < repeater.count; ++i) {
            var item = repeater.itemAt(i)
            if (item && y >= item.y && y < item.y + item.height + root.spacing) {
                return i
            }
        }
        if (repeater.count > 0) {
            var first = repeater.itemAt(0)
            if (first && y < first.y) {
                return 0
            }
            return repeater.count - 1
        }
        return -1
    }

    NavigationPanel {
        id: navPanel
        name: "Categories"
        section: root.navigationSection
        order: 4
        direction: NavigationPanel.Vertical
        accessible.name: qsTrc("project", "Categories")
    }

    RowLayout {
        width: root.innerWidth
        spacing: 6

        StyledTextLabel {
            Layout.fillWidth: true
            leftPadding: 8
            text: qsTrc("project", "CATEGORIES")
            font: ui.theme.bodyBoldFont
            horizontalAlignment: Text.AlignLeft
            opacity: 0.7
        }

        FlatButton {
            icon: IconCode.PLUS
            transparent: true
            toolTipTitle: qsTrc("project", "New category")

            navigation.name: "NewCategory"
            navigation.panel: navPanel
            navigation.row: 0

            onClicked: root.pageModel.newCategoryInteractive()
        }
    }

    ContextMenuLoader {
        id: contextMenu

        property string categoryName: ""

        onHandleMenuItem: function(itemId) {
            switch (itemId) {
            case "rename": root.pageModel.renameCategoryInteractive(categoryName); break
            case "up": root.pageModel.moveCategory(categoryName, -1); break
            case "down": root.pageModel.moveCategory(categoryName, 1); break
            case "reveal": root.pageModel.revealCategory(categoryName); break
            case "delete": root.pageModel.deleteCategoryInteractive(categoryName); break
            }
        }
    }

    function menuItems(index, count) {
        return [
            { id: "rename", title: qsTrc("project", "Rename…") },
            { id: "up", title: qsTrc("project", "Move up"), enabled: index > 0 },
            { id: "down", title: qsTrc("project", "Move down"), enabled: index < count - 1 },
            { id: "reveal", title: root.revealTitle },
            {},
            { id: "delete", title: qsTrc("project", "Delete category"), icon: IconCode.DELETE_TANK }
        ]
    }

    Repeater {
        id: repeater

        model: root.pageModel ? root.pageModel.categories : []

        delegate: Item {
            id: rowItem

            required property var modelData
            required property int index

            readonly property bool isCurrent: root.pageModel && modelData.name === root.pageModel.currentCategory

            width: root.innerWidth
            height: 42

            Rectangle {
                anchors.fill: parent
                radius: 8
                color: rowItem.isCurrent ? "#3B2442"
                                         : (rowMouse.containsMouse ? ui.theme.buttonColor : "transparent")
                border.color: root.dropTargetIndex === rowItem.index && root.dragSourceIndex !== rowItem.index
                              ? ui.theme.accentColor : "transparent"
                border.width: 1
            }

            NavigationControl {
                id: rowNav
                name: "Category " + rowItem.modelData.name
                panel: navPanel
                row: 1 + rowItem.index
                enabled: rowItem.visible

                accessible.role: MUAccessible.RadioButton
                accessible.name: rowItem.modelData.name
                accessible.checked: rowItem.isCurrent

                onTriggered: root.pageModel.currentCategory = rowItem.modelData.name
            }

            MouseArea {
                id: rowMouse

                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton

                onClicked: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        contextMenu.categoryName = rowItem.modelData.name
                        contextMenu.show(mapToItem(root, mouse.x, mouse.y),
                                         root.menuItems(rowItem.index, repeater.count))
                        return
                    }
                    root.pageModel.currentCategory = rowItem.modelData.name
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 8
                spacing: 8

                // Drag handle: press and drag up or down, release over another category.
                Item {
                    Layout.preferredWidth: 20
                    Layout.fillHeight: true

                    StyledIconLabel {
                        anchors.centerIn: parent
                        iconCode: IconCode.MENU_THREE_DOTS
                        rotation: 90
                        opacity: handleMouse.pressed ? 1.0 : 0.5
                    }

                    MouseArea {
                        id: handleMouse

                        anchors.fill: parent
                        cursorShape: Qt.SizeVerCursor
                        preventStealing: true

                        onPressed: {
                            root.dragSourceIndex = rowItem.index
                            root.dropTargetIndex = rowItem.index
                        }

                        onPositionChanged: function(mouse) {
                            if (pressed) {
                                root.dropTargetIndex = root.indexAt(mapToItem(root, mouse.x, mouse.y).y)
                            }
                        }

                        onReleased: {
                            var from = root.dragSourceIndex
                            var to = root.dropTargetIndex
                            root.dragSourceIndex = -1
                            root.dropTargetIndex = -1
                            if (from >= 0 && to >= 0 && from !== to) {
                                root.pageModel.moveCategory(rowItem.modelData.name, to - from)
                            }
                        }

                        onCanceled: {
                            root.dragSourceIndex = -1
                            root.dropTargetIndex = -1
                        }
                    }
                }

                StyledTextLabel {
                    Layout.fillWidth: true
                    text: rowItem.modelData.name
                    font: rowItem.isCurrent ? ui.theme.bodyBoldFont : ui.theme.bodyFont
                    horizontalAlignment: Text.AlignLeft
                    elide: Text.ElideRight
                }

                Rectangle {
                    Layout.preferredWidth: Math.max(24, countLabel.implicitWidth + 12)
                    Layout.preferredHeight: 22
                    radius: 11
                    color: rowItem.isCurrent ? "#B8007F" : ui.theme.buttonColor

                    StyledTextLabel {
                        id: countLabel
                        anchors.centerIn: parent
                        text: rowItem.modelData.count
                        color: rowItem.isCurrent ? "#FFFFFF" : ui.theme.fontPrimaryColor
                    }
                }
            }
        }
    }

    StyledTextLabel {
        width: root.innerWidth
        leftPadding: 8
        topPadding: 8
        text: qsTrc("project", "Drag to reorder. Right-click to rename or delete.")
        horizontalAlignment: Text.AlignLeft
        wrapMode: Text.Wrap
        opacity: 0.6
    }
}
