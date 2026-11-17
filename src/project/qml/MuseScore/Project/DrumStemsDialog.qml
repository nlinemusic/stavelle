/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

//! Jubelle G9: File › Export drum stems…
StyledDialogView {
    id: root

    title: qsTrc("project/drumstems", "Export drum stems")
    objectName: "DrumStemsDialog"

    contentWidth: 760
    contentHeight: 680
    margins: 24

    DrumStemsModel {
        id: ds
    }

    Component.onCompleted: ds.load()

    component SectionTitle: StyledTextLabel {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignLeft
        font: ui.theme.bodyBoldFont
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            text: ds.hasKit
                  ? qsTrc("project/drumstems", "One audio file for each drum of the kit, or for each group of drums you put together. Only the drums this score plays are listed. Nothing in the score is changed.")
                  : qsTrc("project/drumstems", "This score has no drum kit, so there is nothing to split into stems.")
        }

        RowLayout {
            visible: ds.kits.length > 1
            spacing: 8
            StyledTextLabel { text: qsTrc("project/drumstems", "Drum kit:") }
            StyledDropdown {
                Layout.preferredWidth: 300
                model: ds.kits
                currentIndex: ds.kitIndex
                onActivated: function(i, v) { ds.setKitIndex(v) }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: ds.hasKit
            spacing: 16

            // ---- the drums ----
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 380
                spacing: 6

                SectionTitle { text: qsTrc("project/drumstems", "Drums in this score") }

                StyledListView {
                    id: drumList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: ds.components

                    delegate: RowLayout {
                        id: drumRow
                        required property var modelData
                        width: drumList.width
                        spacing: 8

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            StyledTextLabel {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignLeft
                                text: drumRow.modelData.name
                            }
                            StyledTextLabel {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignLeft
                                opacity: 0.7
                                text: drumRow.modelData.notes
                            }
                        }
                        StyledDropdown {
                            Layout.preferredWidth: 170
                            model: ds.groupChoices
                            currentIndex: drumRow.modelData.group + 1
                            onActivated: function(i, v) { ds.moveToGroup(drumRow.modelData.id, v) }
                        }
                    }
                }
            }

            // ---- the groups ----
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 320
                spacing: 6

                SectionTitle { text: qsTrc("project/drumstems", "Groups") }

                StyledListView {
                    id: groupList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    model: ds.groups

                    delegate: ColumnLayout {
                        id: groupRow
                        required property var modelData
                        width: groupList.width
                        spacing: 2

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            TextInputField {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 100
                                currentText: groupRow.modelData.name
                                onTextEditingFinished: function(newText) { ds.renameGroup(groupRow.modelData.index, newText) }
                            }
                            FlatButton {
                                icon: IconCode.DELETE_TANK
                                toolTipTitle: qsTrc("project/drumstems", "Delete this group (its drums stay in the list)")
                                onClicked: ds.removeGroup(groupRow.modelData.index)
                            }
                        }
                        StyledTextLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            wrapMode: Text.Wrap
                            opacity: 0.7
                            text: groupRow.modelData.contents
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    TextInputField {
                        id: newGroupName
                        Layout.fillWidth: true
                        Layout.preferredWidth: 100   // its own width is its parent's: without this the row lays itself out forever
                        hint: qsTrc("project/drumstems", "New group name")
                        onTextChanged: function(newText) { newGroupName.currentText = newText }
                    }
                    FlatButton {
                        text: qsTrc("project/drumstems", "Add group")
                        enabled: newGroupName.currentText !== ""
                        onClicked: {
                            ds.addGroup(newGroupName.currentText)
                            newGroupName.currentText = ""
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    StyledDropdown {
                        id: presetBox
                        Layout.fillWidth: true
                        enabled: ds.presets.length > 0
                        model: ds.presets.map(function(n) { return { value: n, text: n } })
                        displayText: ds.presets.length > 0 ? currentText : qsTrc("project/drumstems", "No saved presets yet")
                        property string chosen: ""
                        currentIndex: ds.presets.indexOf(chosen)
                        onActivated: function(i, v) {
                            presetBox.chosen = v
                            ds.loadPreset(v)
                        }
                    }
                    FlatButton {
                        icon: IconCode.DELETE_TANK
                        enabled: presetBox.chosen !== ""
                        toolTipTitle: qsTrc("project/drumstems", "Delete the chosen preset")
                        onClicked: {
                            ds.deletePreset(presetBox.chosen)
                            presetBox.chosen = ""
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    TextInputField {
                        id: presetName
                        Layout.fillWidth: true
                        Layout.preferredWidth: 100   // its own width is its parent's: without this the row lays itself out forever
                        hint: qsTrc("project/drumstems", "Save these groups as…")
                        onTextChanged: function(newText) { presetName.currentText = newText }
                    }
                    FlatButton {
                        text: qsTrc("project/drumstems", "Save preset")
                        enabled: presetName.currentText !== ""
                        onClicked: {
                            ds.savePreset(presetName.currentText)
                            presetBox.chosen = presetName.currentText
                            presetName.currentText = ""
                        }
                    }
                    FlatButton {
                        text: qsTrc("project/drumstems", "Reset")
                        toolTipTitle: qsTrc("project/drumstems", "Back to Kick, Snare, Hi-hat, Toms, Cymbals, Percussion")
                        onClicked: ds.resetGroups()
                    }
                }
            }
        }

        SeparatorLine { visible: ds.hasKit }

        RowLayout {
            visible: ds.hasKit
            spacing: 16
            CheckBox {
                text: qsTrc("project/drumstems", "A file for each group")
                checked: ds.exportGroups
                onClicked: ds.setExportGroups(!checked)
            }
            CheckBox {
                text: qsTrc("project/drumstems", "A file for each single drum")
                checked: ds.exportEach
                onClicked: ds.setExportEach(!checked)
            }
            Item { Layout.fillWidth: true }
            StyledTextLabel { text: qsTrc("project/drumstems", "Format:") }
            StyledDropdown {
                Layout.preferredWidth: 100
                model: ds.formats
                currentIndex: ds.formatIndex
                onActivated: function(i, v) { ds.setFormatIndex(v) }
            }
        }

        RowLayout {
            visible: ds.hasKit
            Layout.fillWidth: true
            spacing: 8
            StyledTextLabel { text: qsTrc("project/drumstems", "Folder:") }
            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                elide: Text.ElideMiddle
                text: ds.folder !== "" ? ds.folder : qsTrc("project/drumstems", "not chosen yet")
            }
            FlatButton {
                text: qsTrc("project/drumstems", "Choose…")
                onClicked: ds.chooseFolder()
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            visible: text !== ""
            text: ds.status
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Item { Layout.fillWidth: true }
            FlatButton {
                text: qsTrc("global", "Close")
                enabled: !ds.exporting
                onClicked: root.hide()
            }
            FlatButton {
                accentButton: true
                visible: ds.hasKit
                enabled: !ds.exporting && ds.fileCount > 0
                text: qsTrc("project/drumstems", "Export %n file(s)", "", ds.fileCount)
                onClicked: ds.exportStems()
            }
        }
    }
}
