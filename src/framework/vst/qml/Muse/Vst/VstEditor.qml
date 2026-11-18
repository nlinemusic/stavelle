/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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
import Muse.Vst

Rectangle {
    id: root

    // in
    property alias instanceId: view.instanceId

    // out
    property alias title: view.title

    color: ui.theme.backgroundPrimaryColor

    implicitWidth: Math.max(view.implicitWidth, presetBar.implicitWidth + 24)
    // Jubelle: the plugin's own window sits below the Presets bar
    implicitHeight: view.implicitHeight + view.topPadding

    Component.onCompleted: {
        view.init()
    }

    // Jubelle: Presets bar. Lists this plugin's .vstpreset files (from the
    // standard VST3 preset folders) and loads one; the result saves with the
    // score like any other change to the plugin's settings.
    VstPresetsModel {
        id: presetsModel
        instanceId: view.instanceId
    }

    // Jubelle: level trim, bend range and key switches of this instrument (saved with the score)
    VstInstrumentSettingsModel {
        id: instrumentSettings
        instanceId: view.instanceId
    }

    property bool showInstrumentSettings: false

    Row {
        id: presetBar
        anchors.top: parent.top
        anchors.topMargin: 8
        anchors.left: parent.left
        anchors.leftMargin: 12
        height: 32
        spacing: 8

        StyledTextLabel {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTrc("vst", "Preset")
        }

        StyledDropdown {
            id: presetDropdown
            width: 260
            anchors.verticalCenter: parent.verticalCenter
            enabled: presetsModel.presets.length > 0
            model: presetsModel.presets
            textRole: "title"
            valueRole: "path"
            currentIndex: -1
            displayText: presetsModel.currentPresetTitle !== "" ? presetsModel.currentPresetTitle
                                                                : (presetsModel.presets.length > 0 ? qsTrc("vst", "Choose a preset…")
                                                                                                   : qsTrc("vst", "No preset files"))
            onActivated: function(index, value) {
                presetsModel.applyPreset(index)
            }
        }

        FlatButton {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTrc("vst", "Open preset file…")
            onClicked: presetsModel.openPresetFile()
        }

        FlatButton {
            anchors.verticalCenter: parent.verticalCenter
            text: root.showInstrumentSettings ? qsTrc("vst", "Show the instrument") : qsTrc("vst", "Instrument settings…")
            toolTipTitle: qsTrc("vst", "Level trim, pitch-bend range and key switches for this instrument")
            onClicked: root.showInstrumentSettings = !root.showInstrumentSettings
        }

        StyledTextLabel {
            anchors.verticalCenter: parent.verticalCenter
            visible: text !== ""
            text: presetsModel.message
            opacity: 0.75
        }
    }

    // The plugin's own window is drawn by the plugin and would cover anything placed over it,
    // so the settings take its place while they are shown
    StyledFlickable {
        id: settingsPane
        visible: root.showInstrumentSettings
        anchors.fill: parent
        anchors.topMargin: presetBar.height + 16
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        contentHeight: settingsColumn.implicitHeight + 24
        clip: true

        Column {
            id: settingsColumn
            width: Math.min(settingsPane.width, 560)
            spacing: 12

            Row {
                spacing: 8
                StyledTextLabel {
                    width: 200
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignLeft
                    text: qsTrc("vst", "Level trim")
                }
                IncrementalPropertyControl {
                    width: 120
                    currentValue: instrumentSettings.trimDb
                    minValue: -24
                    maxValue: 12
                    step: 0.5
                    decimals: 1
                    measureUnitsSymbol: qsTrc("vst", "dB")
                    onValueEdited: function(v) { instrumentSettings.trimDb = v }
                }
            }

            StyledTextLabel {
                width: parent.width
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: qsTrc("vst", "Makes this instrument louder or quieter than the others without changing its dynamics.")
            }

            Row {
                spacing: 8
                StyledTextLabel {
                    width: 200
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignLeft
                    text: qsTrc("vst", "Pitch-bend range of the library")
                }
                IncrementalPropertyControl {
                    width: 120
                    currentValue: instrumentSettings.bendRange
                    minValue: 1
                    maxValue: 48
                    step: 1
                    decimals: 0
                    measureUnitsSymbol: qsTrc("vst", "st")
                    onValueEdited: function(v) { instrumentSettings.bendRange = v }
                }
            }

            StyledTextLabel {
                width: parent.width
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: qsTrc("vst", "Set this to the bend range in the library's own settings (2 semitones is the usual default). Then glissandos and falls wider than that glide smoothly instead of jumping.")
            }

            SeparatorLine {}

            StyledTextLabel {
                width: parent.width
                horizontalAlignment: Text.AlignLeft
                font: ui.theme.bodyBoldFont
                text: qsTrc("vst", "Key switches")
            }

            StyledTextLabel {
                width: parent.width
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: qsTrc("vst", "If this library changes articulation with key switches, enter the key for each one (a note name such as C1 or F#0, or a MIDI number). Before each note Jubelle sends the key for its articulation, or the Normal key when it has none of these. Leave a box empty for none.")
            }

            Row {
                spacing: 8
                StyledTextLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTrc("vst", "The library calls middle C")
                }
                RoundedRadioButton {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "C4"
                    checked: instrumentSettings.middleCIsC4
                    onToggled: instrumentSettings.middleCIsC4 = true
                }
                RoundedRadioButton {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "C3"
                    checked: !instrumentSettings.middleCIsC4
                    onToggled: instrumentSettings.middleCIsC4 = false
                }
            }

            Repeater {
                model: instrumentSettings.keySwitches

                delegate: Row {
                    required property var modelData
                    spacing: 8

                    StyledTextLabel {
                        width: 200
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignLeft
                        text: modelData.title
                    }

                    TextInputField {
                        id: keyField
                        width: 120
                        currentText: modelData.note
                        hint: qsTrc("vst", "none")
                        onTextEditingFinished: function(newText) {
                            if (!instrumentSettings.setKeySwitch(modelData.group, newText)) {
                                keyField.currentText = modelData.note
                            }
                        }
                    }
                }
            }

            FlatButton {
                text: qsTrc("vst", "Clear all key switches")
                onClicked: instrumentSettings.clearKeySwitches()
            }
        }
    }

    VstView {
        id: view
        visible: !root.showInstrumentSettings
        anchors.fill: parent
        topPadding: presetBar.height + 16
    }
}
