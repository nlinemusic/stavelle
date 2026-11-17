/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 * Music Composition & Notation
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
import QtQuick

import Muse.Ui
import Muse.UiComponents

//! Jubelle R1: "Add to Projects…" / "Import MuseScore files…": choose the
//! category (and, for one score, the project name). A copy is made; the
//! original file is never moved or changed.
StyledDialogView {
    id: root

    property string initialName: ""
    property bool askName: true
    property int fileCount: 1
    property var categories: []

    objectName: "AddToProjectsDialog"

    contentWidth: 520
    contentHeight: content.implicitHeight
    margins: 24

    modal: true

    property string currentName: initialName
    property string selectedCategory: categories.length > 0 ? categories[0] : ""

    function done() {
        root.ret = { errcode: 0, value: { "category": root.selectedCategory, "name": root.currentName.trim() } }
        root.hide()
    }

    Column {
        id: content

        width: parent.width
        spacing: 16

        StyledTextLabel {
            width: parent.width
            text: root.fileCount > 1 ? qsTrc("project", "Add %1 scores to Jubelle Projects").arg(root.fileCount)
                                     : qsTrc("project", "Add to Jubelle Projects")
            font: ui.theme.largeBodyBoldFont
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
        }

        Column {
            width: parent.width
            spacing: 8
            visible: root.askName

            StyledTextLabel {
                text: qsTrc("project", "Project name")
            }

            TextInputField {
                id: nameField

                width: parent.width
                currentText: root.initialName

                navigation.panel: NavigationPanel {
                    name: "AddToProjectsName"
                    section: root.navigationSection
                    order: 1
                }

                onTextChanged: function(newTextValue) {
                    root.currentName = newTextValue
                }
            }
        }

        StyledTextLabel {
            text: qsTrc("project", "Category")
        }

        NavigationPanel {
            id: listNavPanel
            name: "AddToProjectsCategories"
            section: root.navigationSection
            direction: NavigationPanel.Vertical
            order: 2
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
                    text: modelData
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
            text: qsTrc("project", "Jubelle makes a copy in a new project folder, with its Backups and Exports folders. Your original file is never moved or changed.")
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
                text: qsTrc("project", "Add to Projects")
                accentButton: true
                enabled: root.selectedCategory !== "" && (!root.askName || root.currentName.trim() !== "")
                buttonRole: ButtonBoxModel.AcceptRole
                buttonId: ButtonBoxModel.Ok

                onClicked: root.done()
            }
        }
    }
}
