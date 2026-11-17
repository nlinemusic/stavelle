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
import MuseScore.NotationScene

//! Jubelle: save the current score's engraving settings as a named preset and
//! apply presets to any score. Presets are ordinary style files (.mss).
StyledDialogView {
    id: root

    title: qsTrc("notation", "Engraving presets")
    objectName: "EngravingPresetsDialog"

    contentWidth: 520
    contentHeight: content.implicitHeight
    margins: 24

    modal: true

    property string newName: ""
    property string selectedPath: ""
    property string message: ""

    EngravingPresetsModel {
        id: presetsModel
    }

    Component.onCompleted: presetsModel.load()

    Column {
        id: content

        width: parent.width
        spacing: 16

        StyledTextLabel {
            width: parent.width
            text: qsTrc("notation", "A preset stores every setting of Format → Style (spacing, beams, slurs, ties, articulations, dynamics, fonts, page layout). Applying one changes only this score and can be undone.")
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            opacity: 0.8
        }

        NavigationPanel {
            id: listPanel
            name: "EngravingPresetsList"
            section: root.navigationSection
            direction: NavigationPanel.Vertical
            order: 1
        }

        StyledTextLabel {
            width: parent.width
            visible: presetsModel.presets.length === 0
            text: qsTrc("notation", "No presets yet. Save this score’s style as your first preset below.")
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
        }

        Column {
            width: parent.width
            spacing: 6
            visible: presetsModel.presets.length > 0

            Repeater {
                model: presetsModel.presets

                delegate: RoundedRadioButton {
                    required property var modelData
                    required property int index

                    width: parent.width
                    text: modelData.name
                    checked: modelData.path === root.selectedPath

                    navigation.name: modelData.name
                    navigation.panel: listPanel
                    navigation.row: index

                    onToggled: root.selectedPath = modelData.path
                }
            }
        }

        Row {
            spacing: 8

            NavigationPanel {
                id: listActionsPanel
                name: "EngravingPresetsActions"
                section: root.navigationSection
                order: 2
            }

            FlatButton {
                text: qsTrc("notation", "Apply to this score")
                accentButton: true
                enabled: root.selectedPath !== ""
                navigation.panel: listActionsPanel
                navigation.order: 1
                onClicked: {
                    if (presetsModel.apply(root.selectedPath)) {
                        root.message = qsTrc("notation", "Preset applied.")
                    }
                }
            }

            FlatButton {
                text: qsTrc("notation", "Show in folder")
                enabled: root.selectedPath !== ""
                navigation.panel: listActionsPanel
                navigation.order: 2
                onClicked: presetsModel.reveal(root.selectedPath)
            }

            FlatButton {
                text: qsTrc("notation", "Open presets folder")
                navigation.panel: listActionsPanel
                navigation.order: 3
                onClicked: presetsModel.openFolder()
            }
        }

        SeparatorLine { width: parent.width }

        StyledTextLabel {
            text: qsTrc("notation", "Save this score’s style as a preset")
            font: ui.theme.bodyBoldFont
        }

        Row {
            spacing: 8

            NavigationPanel {
                id: savePanel
                name: "EngravingPresetsSave"
                section: root.navigationSection
                order: 3
            }

            TextInputField {
                width: 280
                hint: qsTrc("notation", "Preset name, e.g. Big band parts")
                currentText: root.newName
                navigation.panel: savePanel
                navigation.order: 1

                onTextChanged: function(newTextValue) {
                    root.newName = newTextValue
                }
            }

            FlatButton {
                text: qsTrc("notation", "Save preset")
                enabled: root.newName.trim() !== "" && presetsModel.validateName(root.newName) === ""
                navigation.panel: savePanel
                navigation.order: 2
                onClicked: {
                    if (presetsModel.saveCurrentStyle(root.newName)) {
                        root.message = qsTrc("notation", "Preset “%1” saved.").arg(root.newName.trim())
                        root.newName = ""
                    }
                }
            }
        }

        StyledTextLabel {
            width: parent.width
            visible: text !== ""
            text: root.newName.trim() !== "" ? presetsModel.validateName(root.newName) : root.message
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
        }

        ButtonBox {
            width: parent.width

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 4

            FlatButton {
                text: qsTrc("global", "Close")
                buttonRole: ButtonBoxModel.AcceptRole
                buttonId: ButtonBoxModel.Close
                onClicked: root.hide()
            }
        }
    }
}
