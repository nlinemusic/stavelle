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

import Muse.Ui
import Muse.UiComponents

//! Jubelle: "Move to category…" for a project.
StyledDialogView {
    id: root

    property string projectName: ""
    property var categories: []
    property string currentCategory: ""

    objectName: "ProjectMoveDialog"

    contentWidth: 520
    contentHeight: content.implicitHeight
    margins: 24

    modal: true

    property string selectedCategory: ""

    function done() {
        root.ret = { errcode: 0, value: { "category": root.selectedCategory } }
        root.hide()
    }

    Column {
        id: content

        width: parent.width
        spacing: 16

        StyledTextLabel {
            width: parent.width
            text: qsTrc("project", "Move “%1” to…").arg(root.projectName)
            font: ui.theme.largeBodyBoldFont
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
        }

        NavigationPanel {
            id: listNavPanel
            name: "MoveCategoryList"
            section: root.navigationSection
            direction: NavigationPanel.Vertical
            order: 1
        }

        Column {
            width: parent.width
            spacing: 6

            Repeater {
                model: root.categories

                delegate: RoundedRadioButton {
                    required property var modelData
                    required property int index

                    width: parent.width
                    text: modelData === root.currentCategory
                          ? qsTrc("project", "%1 (current)").arg(modelData)
                          : modelData
                    enabled: modelData !== root.currentCategory
                    checked: modelData === root.selectedCategory

                    navigation.name: modelData
                    navigation.panel: listNavPanel
                    navigation.row: index

                    onToggled: {
                        root.selectedCategory = modelData
                    }
                }
            }
        }

        StyledTextLabel {
            width: parent.width
            text: qsTrc("project", "The score, its backups, exports and card image move together. To list it in several categories at once, use Categories and tags… instead. If the category already has a project with this name, nothing is moved.")
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            opacity: 0.8
        }

        ButtonBox {
            width: parent.width

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 2

            FlatButton {
                text: qsTrc("global", "Cancel")
                buttonRole: ButtonBoxModel.RejectRole
                buttonId: ButtonBoxModel.Cancel

                onClicked: root.reject()
            }

            FlatButton {
                text: qsTrc("project", "Move")
                accentButton: true
                enabled: root.selectedCategory !== "" && root.selectedCategory !== root.currentCategory
                buttonRole: ButtonBoxModel.AcceptRole
                buttonId: ButtonBoxModel.Ok

                onClicked: root.done()
            }
        }
    }
}
