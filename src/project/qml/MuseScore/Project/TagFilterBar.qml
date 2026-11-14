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

//! Jubelle: tag filter for the selected category. Select one or more tags; the
//! MATCH switch chooses between "All selected tags" (default) and "Any selected tag".
Rectangle {
    id: root

    property var pageModel: null
    property NavigationSection navigationSection: null

    property bool addingTag: false
    property string draftTag: ""

    height: column.implicitHeight
    radius: 10
    color: ui.theme.backgroundSecondaryColor
    border.color: ui.theme.strokeColor
    border.width: 1

    NavigationPanel {
        id: navPanel
        name: "TagFilter"
        section: root.navigationSection
        order: 5
        direction: NavigationPanel.Horizontal
        accessible.name: qsTrc("project", "Filter by tag")
    }

    Column {
        id: column

        width: parent.width
        padding: 14
        spacing: 12

        Flow {
            width: parent.width - 2 * parent.padding
            spacing: 8

            StyledTextLabel {
                height: 30
                verticalAlignment: Text.AlignVCenter
                rightPadding: 4
                text: qsTrc("project", "Tags")
                font: ui.theme.bodyBoldFont
            }

            Repeater {
                model: root.pageModel ? root.pageModel.availableTags : []

                delegate: FlatButton {
                    required property var modelData
                    required property int index

                    readonly property bool isActive: root.pageModel.activeTags.indexOf(modelData) >= 0

                    height: 30
                    text: (isActive ? "✓ " : "") + modelData
                    accentButton: isActive

                    navigation.name: "Tag " + modelData
                    navigation.panel: navPanel
                    navigation.column: index
                    accessible.checked: isActive

                    onClicked: root.pageModel.toggleTag(modelData)
                }
            }

            FlatButton {
                height: 30
                visible: !root.addingTag
                text: qsTrc("project", "+ New tag")
                transparent: true

                navigation.name: "NewTag"
                navigation.panel: navPanel
                navigation.column: 1000

                onClicked: {
                    root.draftTag = ""
                    root.addingTag = true
                }
            }

            Row {
                visible: root.addingTag
                spacing: 6

                TextInputField {
                    width: 150
                    height: 30
                    hint: qsTrc("project", "Tag name")
                    currentText: root.draftTag

                    navigation.panel: navPanel
                    navigation.column: 1001

                    onTextChanged: function(newTextValue) {
                        root.draftTag = newTextValue
                    }

                    onAccepted: {
                        if (root.draftTag.trim() !== "") {
                            root.pageModel.addTag(root.draftTag)
                        }
                        root.addingTag = false
                    }

                    onEscaped: root.addingTag = false
                }

                FlatButton {
                    height: 30
                    text: qsTrc("project", "Add")
                    accentButton: true
                    enabled: root.draftTag.trim() !== ""

                    navigation.panel: navPanel
                    navigation.column: 1002

                    onClicked: {
                        root.pageModel.addTag(root.draftTag)
                        root.addingTag = false
                    }
                }

                FlatButton {
                    height: 30
                    icon: IconCode.CLOSE_X_ROUNDED
                    transparent: true
                    toolTipTitle: qsTrc("global", "Cancel")

                    navigation.panel: navPanel
                    navigation.column: 1003

                    onClicked: root.addingTag = false
                }
            }

            // MATCH switch
            Row {
                spacing: 2

                StyledTextLabel {
                    height: 30
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 12
                    rightPadding: 6
                    text: qsTrc("project", "MATCH")
                    opacity: 0.7
                }

                FlatButton {
                    height: 30
                    text: qsTrc("project", "All selected tags")
                    accentButton: root.pageModel && root.pageModel.matchAllTags
                    transparent: !accentButton

                    navigation.name: "MatchAll"
                    navigation.panel: navPanel
                    navigation.column: 2000
                    accessible.checked: accentButton

                    onClicked: root.pageModel.matchAllTags = true
                }

                FlatButton {
                    height: 30
                    text: qsTrc("project", "Any selected tag")
                    accentButton: root.pageModel && !root.pageModel.matchAllTags
                    transparent: !accentButton

                    navigation.name: "MatchAny"
                    navigation.panel: navPanel
                    navigation.column: 2001
                    accessible.checked: accentButton

                    onClicked: root.pageModel.matchAllTags = false
                }
            }
        }

        SeparatorLine {
            visible: root.pageModel && root.pageModel.activeTags.length > 0
            width: parent.width - 2 * parent.padding
        }

        Flow {
            width: parent.width - 2 * parent.padding
            spacing: 8
            visible: root.pageModel && root.pageModel.activeTags.length > 0

            StyledTextLabel {
                height: 30
                verticalAlignment: Text.AlignVCenter
                text: root.pageModel && root.pageModel.matchAllTags
                      ? qsTrc("project", "Showing scores with ALL of:")
                      : qsTrc("project", "Showing scores with ANY of:")
                opacity: 0.8
            }

            Repeater {
                model: root.pageModel ? root.pageModel.activeTags : []

                delegate: FlatButton {
                    required property var modelData
                    required property int index

                    height: 30
                    text: modelData + "  ×"
                    accentButton: true
                    toolTipTitle: qsTrc("project", "Remove filter %1").arg(modelData)

                    navigation.name: "RemoveFilter " + modelData
                    navigation.panel: navPanel
                    navigation.column: 3000 + index

                    onClicked: root.pageModel.removeActiveTag(modelData)
                }
            }

            FlatButton {
                height: 30
                text: qsTrc("project", "Clear all filters")
                transparent: true

                navigation.name: "ClearAllFilters"
                navigation.panel: navPanel
                navigation.column: 4000

                onClicked: root.pageModel.clearTags()
            }
        }
    }
}
