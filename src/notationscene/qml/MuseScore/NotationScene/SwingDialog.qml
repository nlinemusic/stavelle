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
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.NotationScene

//! Jubelle: Format → Swing…  The rhythmic feel for the whole score and for
//! single instruments, and the feels themselves. Playback only; the written
//! notes never change. Nothing changes until Apply (one undo step).
StyledDialogView {
    id: root

    title: qsTrc("notation", "Swing")
    objectName: "SwingDialog"

    contentWidth: 600
    contentHeight: 660
    margins: 24

    modal: true

    readonly property color jubelleMagenta: "#E600A9"
    readonly property color jubellePurple: "#A855F7"
    readonly property var feel: swingModel.editedFeel

    SwingModel {
        id: swingModel
    }

    Component.onCompleted: swingModel.load()

    NavigationPanel {
        id: panel
        name: "SwingDialogPanel"
        section: root.navigationSection
        direction: NavigationPanel.Vertical
        order: 1
    }

    component SectionTitle: StyledTextLabel {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignLeft
        font: ui.theme.bodyBoldFont
        color: root.jubellePurple
    }

    component RowLabel: StyledTextLabel {
        Layout.preferredWidth: 200
        horizontalAlignment: Text.AlignLeft
    }

    component RatioControl: RowLayout {
        id: ratioRow
        property string key
        property real value
        property string percent
        spacing: 8

        IncrementalPropertyControl {
            Layout.preferredWidth: 110
            currentValue: ratioRow.value
            step: 0.05
            decimals: 2
            minValue: 1.0
            maxValue: 4.0
            measureUnitsSymbol: ": 1"
            navigation.panel: panel
            onValueEdited: function(newValue) { swingModel.setFeelValue(ratioRow.key, newValue) }
        }
        StyledTextLabel {
            text: qsTrc("notation", "(long note = %1 of the beat)").arg(ratioRow.percent)
            opacity: 0.7
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // ---- Which feel is used where ----
        SectionTitle { text: qsTrc("notation", "Feel for the whole score") }

        StyledDropdown {
            Layout.fillWidth: true
            model: [{ text: qsTrc("notation", "Not set (MuseScore's swing from Format → Style)") }].concat(swingModel.feelChoices)
            currentIndex: swingModel.scoreFeelIndex
            navigation.panel: panel
            onActivated: function(index, value) { swingModel.scoreFeelIndex = index }
        }

        SectionTitle { text: qsTrc("notation", "Instruments with their own feel") }

        StyledListView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 140)
            spacing: 4
            clip: true
            model: swingModel.instruments

            delegate: RowLayout {
                width: ListView.view.width
                spacing: 8

                StyledTextLabel {
                    Layout.preferredWidth: 200
                    horizontalAlignment: Text.AlignLeft
                    text: modelData.name
                }
                StyledDropdown {
                    Layout.fillWidth: true
                    model: [{ text: qsTrc("notation", "Same as whole score") }].concat(swingModel.feelChoices)
                    currentIndex: modelData.choice
                    navigation.panel: panel
                    onActivated: function(i, value) { swingModel.setInstrumentFeel(index, i) }
                }
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            opacity: 0.7
            text: qsTrc("notation", "To change the feel from a point in the score, add a Swing text there and choose a feel in its Staff text properties.")
        }

        // ---- The marking near the tempo marking ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            RowLabel { text: qsTrc("notation", "Marking above the tempo:") }
            StyledDropdown {
                Layout.fillWidth: true
                model: [
                    { text: qsTrc("notation", "None") },
                    { text: qsTrc("notation", "Feel name, e.g. “Basie swing”") },
                    { text: qsTrc("notation", "Feel name and rhythmic equation") },
                    { text: qsTrc("notation", "My own description") }
                ]
                currentIndex: swingModel.markingStyle
                navigation.panel: panel
                onActivated: function(index, value) { swingModel.markingStyle = index }
            }
        }
        TextInputField {
            Layout.fillWidth: true
            visible: swingModel.markingStyle === 3
            currentText: swingModel.markingText
            hint: qsTrc("notation", "e.g. Relaxed swing")
            navigation.panel: panel
            onTextEditingFinished: function(newTextValue) { swingModel.markingText = newTextValue }
        }

        SeparatorLine { Layout.fillWidth: true }

        // ---- Editing a feel ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            SectionTitle {
                Layout.fillWidth: false
                text: qsTrc("notation", "Edit feel:")
            }
            StyledDropdown {
                Layout.fillWidth: true
                model: swingModel.editableFeels
                currentIndex: swingModel.editedFeelIndex
                navigation.panel: panel
                onActivated: function(index, value) { swingModel.editedFeelIndex = index }
            }
            FlatButton {
                text: qsTrc("notation", "New from this")
                navigation.panel: panel
                onClicked: swingModel.newCustomFeel()
            }
        }

        RowLayout {
            visible: !root.feel.builtIn
            spacing: 8
            RowLabel { text: qsTrc("notation", "Name:") }
            TextInputField {
                Layout.fillWidth: true
                currentText: root.feel.name || ""
                navigation.panel: panel
                onTextEditingFinished: function(newTextValue) { swingModel.setFeelValue("name", newTextValue) }
            }
        }

        RowLayout {
            enabled: !root.feel.isStraight
            spacing: 16
            RowLabel { text: qsTrc("notation", "Swing:") }
            RoundedRadioButton {
                text: qsTrc("notation", "Eighths")
                checked: root.feel.unit === 8
                onToggled: swingModel.setFeelValue("unit", 8)
            }
            RoundedRadioButton {
                text: qsTrc("notation", "Sixteenths")
                checked: root.feel.unit === 16
                onToggled: swingModel.setFeelValue("unit", 16)
            }
        }

        CheckBox {
            enabled: !root.feel.isStraight
            text: qsTrc("notation", "Depends on tempo (more swing when slow, straighter when fast)")
            checked: root.feel.tempoDependent === true
            navigation.panel: panel
            onClicked: swingModel.setFeelValue("tempoDependent", !checked)
        }

        GridLayout {
            visible: root.feel.tempoDependent === true && !root.feel.isStraight
            columns: 3
            columnSpacing: 8
            rowSpacing: 6

            RowLabel { text: qsTrc("notation", "At or below") }
            IncrementalPropertyControl {
                Layout.preferredWidth: 110
                currentValue: root.feel.lowTempo || 80
                step: 5; decimals: 0; minValue: 20; maxValue: 400
                measureUnitsSymbol: qsTrc("notation", "bpm")
                navigation.panel: panel
                onValueEdited: function(newValue) { swingModel.setFeelValue("lowTempo", newValue) }
            }
            RatioControl { key: "lowTempoRatio"; value: root.feel.lowTempoRatio || 1; percent: root.feel.lowTempoPercent || "" }

            RowLabel { text: qsTrc("notation", "At or above") }
            IncrementalPropertyControl {
                Layout.preferredWidth: 110
                currentValue: root.feel.highTempo || 240
                step: 5; decimals: 0; minValue: 20; maxValue: 400
                measureUnitsSymbol: qsTrc("notation", "bpm")
                navigation.panel: panel
                onValueEdited: function(newValue) { swingModel.setFeelValue("highTempo", newValue) }
            }
            RatioControl { key: "highTempoRatio"; value: root.feel.highTempoRatio || 1; percent: root.feel.highTempoPercent || "" }
        }

        RowLayout {
            visible: root.feel.tempoDependent !== true && !root.feel.isStraight
            spacing: 8
            RowLabel { text: qsTrc("notation", "Swing amount (long : short)") }
            RatioControl { key: "ratio"; value: root.feel.ratio || 1; percent: root.feel.ratioPercent || "" }
        }

        RowLayout {
            enabled: !root.feel.isStraight
            spacing: 8
            RowLabel { text: qsTrc("notation", "Swung offbeats: loudness") }
            IncrementalPropertyControl {
                Layout.preferredWidth: 110
                currentValue: root.feel.offbeatLoudness || 100
                step: 1; decimals: 0; minValue: 50; maxValue: 120
                measureUnitsSymbol: "%"
                navigation.panel: panel
                onValueEdited: function(newValue) { swingModel.setFeelValue("offbeatLoudness", Math.round(newValue)) }
            }
            StyledTextLabel { text: qsTrc("notation", "length"); opacity: 0.8 }
            IncrementalPropertyControl {
                Layout.preferredWidth: 110
                currentValue: root.feel.offbeatLength || 100
                step: 1; decimals: 0; minValue: 50; maxValue: 100
                measureUnitsSymbol: "%"
                navigation.panel: panel
                onValueEdited: function(newValue) { swingModel.setFeelValue("offbeatLength", Math.round(newValue)) }
            }
        }

        RowLayout {
            spacing: 8
            FlatButton {
                visible: root.feel.builtIn === true
                enabled: root.feel.edited === true
                text: qsTrc("notation", "Restore built-in values")
                navigation.panel: panel
                onClicked: swingModel.restoreEditedFeel()
            }
            FlatButton {
                visible: root.feel.builtIn !== true
                text: qsTrc("notation", "Delete this feel")
                navigation.panel: panel
                onClicked: swingModel.deleteEditedFeel()
            }
        }

        Item { Layout.fillHeight: true }

        StyledTextLabel {
            Layout.fillWidth: true
            visible: text !== ""
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            color: root.jubelleMagenta
            text: swingModel.resultText
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            NavigationPanel {
                id: buttonsPanel
                name: "SwingDialogButtons"
                section: root.navigationSection
                direction: NavigationPanel.Horizontal
                order: 2
            }

            Item { Layout.fillWidth: true }

            FlatButton {
                text: swingModel.hasChanges ? qsTrc("global", "Cancel") : qsTrc("global", "Close")
                navigation.panel: buttonsPanel
                navigation.column: 1
                onClicked: root.hide()
            }
            FlatButton {
                text: qsTrc("global", "Apply")
                accentButton: true
                enabled: swingModel.hasChanges
                navigation.panel: buttonsPanel
                navigation.column: 2
                onClicked: swingModel.apply()
            }
        }
    }
}
