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

//! Jubelle: asks for a name (rename project, new category, rename category).
StyledDialogView {
    id: root

    property string heading: ""
    property string fieldLabel: ""
    property string initialName: ""
    property string note: ""
    property string acceptText: qsTrc("global", "OK")
    property bool showPrintedTitleOption: false

    objectName: "ProjectNameDialog"

    contentWidth: 520
    contentHeight: content.implicitHeight
    margins: 24

    modal: true

    property string currentName: initialName

    function done() {
        root.ret = { errcode: 0, value: { "name": root.currentName.trim(), "changeTitle": titleCheckBox.checked } }
        root.hide()
    }

    onNavigationActivateRequested: {
        nameField.navigation.requestActive()
    }

    Column {
        id: content

        width: parent.width
        spacing: 18

        StyledTextLabel {
            width: parent.width
            text: root.heading
            font: ui.theme.largeBodyBoldFont
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
        }

        Column {
            width: parent.width
            spacing: 8

            StyledTextLabel {
                text: root.fieldLabel
                visible: root.fieldLabel !== ""
            }

            TextInputField {
                id: nameField

                width: parent.width
                currentText: root.initialName

                navigation.panel: NavigationPanel {
                    name: "ProjectNameField"
                    section: root.navigationSection
                    order: 1
                }

                onTextChanged: function(newTextValue) {
                    root.currentName = newTextValue
                }

                onAccepted: {
                    if (okButton.enabled) {
                        root.done()
                    }
                }
            }
        }

        StyledTextLabel {
            width: parent.width
            visible: root.note !== ""
            text: root.note
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            opacity: 0.8
        }

        CheckBox {
            id: titleCheckBox

            width: parent.width
            visible: root.showPrintedTitleOption
            checked: false
            text: qsTrc("project", "Also change the title printed on the score")

            navigation.panel: NavigationPanel {
                name: "ProjectNameOptions"
                section: root.navigationSection
                order: 2
            }

            onClicked: {
                titleCheckBox.checked = !titleCheckBox.checked
            }
        }

        StyledTextLabel {
            width: parent.width
            visible: root.showPrintedTitleOption && titleCheckBox.checked
            text: qsTrc("project", "The score opens with the new title. Save it to keep the change.")
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
                id: okButton

                text: root.acceptText
                accentButton: true
                enabled: root.currentName.trim() !== ""
                buttonRole: ButtonBoxModel.AcceptRole
                buttonId: ButtonBoxModel.Ok

                onClicked: root.done()
            }
        }
    }
}
