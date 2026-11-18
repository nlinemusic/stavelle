/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle: the "Playback" section of contextual Properties (articulations, jazz
// turn, glissandos). The model gives playbackRows / playbackLevels /
// playbackLevel / playbackNote and setPlaybackValue / clearPlaybackValue (see
// jubelleplaybacksection.h). Playback only: the written music never changes.

import QtQuick

import Muse.Ui
import Muse.UiComponents
import MuseScore.Inspector

Item {
    id: root

    property var model: null
    property NavigationPanel navigationPanel: null
    property int navigationRowStart: 0
    //! For glissandos: the start note's length in ticks (480 = a quarter); shows "Start after" choices
    property int startNoteTicks: 0

    implicitHeight: playbackSection.visible ? playbackSection.implicitHeight : 0
    visible: playbackSection.visible

    Column {
        id: playbackSection

        width: parent.width
        spacing: 8
        visible: root.model ? root.model.playbackRows.length > 0 : false

        readonly property color jubelleMagenta: "#E600A9"

        SeparatorLine { anchors.margins: -12 }

        StyledTextLabel {
            width: parent.width
            horizontalAlignment: Text.AlignLeft
            font: ui.theme.bodyBoldFont
            text: qsTrc("inspector", "Playback")
        }

        StyledTextLabel {
            width: parent.width
            horizontalAlignment: Text.AlignLeft
            text: qsTrc("inspector", "Set for")
        }

        StyledDropdown {
            width: parent.width
            model: root.model ? root.model.playbackLevels : []
            currentIndex: root.model ? root.model.playbackLevel : 0
            navigation.name: "PlaybackLevelDropdown"
            navigation.panel: root.navigationPanel
            navigation.row: root.navigationRowStart
            onActivated: function(index, value) { root.model.playbackLevel = index }
        }

        StyledTextLabel {
            width: parent.width
            visible: text !== ""
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            opacity: 0.7
            text: root.model ? root.model.playbackNote : ""
        }

        Repeater {
            model: root.model ? root.model.playbackRows : []

            delegate: Column {
                width: playbackSection.width
                spacing: 4

                StyledTextLabel {
                    width: parent.width
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.Wrap
                    text: modelData.title
                }

                Row {
                    width: parent.width
                    spacing: 6

                    IncrementalPropertyControl {
                        width: parent.width / 2
                        visible: modelData.unit !== "curve"
                        isIndeterminate: modelData.varies
                        currentValue: modelData.value
                        step: 1
                        decimals: 0
                        minValue: modelData.min
                        maxValue: modelData.max
                        measureUnitsSymbol: modelData.unit === "semitones" ? qsTrc("inspector", "st")
                                            : modelData.unit === "% of normal" ? "%" : modelData.unit
                        navigation.name: "Playback " + modelData.key
                        navigation.panel: root.navigationPanel
                        navigation.row: root.navigationRowStart + 1 + index * 3
                        navigation.accessible.name: modelData.title
                        onValueEdited: function(newValue) { root.model.setPlaybackValue(modelData.key, Math.round(newValue)) }
                    }

                    // Speed curves: three named choices instead of -1 / 0 / +1
                    StyledDropdown {
                        width: parent.width
                        visible: modelData.unit === "curve"
                        model: [
                            { text: qsTrc("inspector", "slows down"), value: -1 },
                            { text: qsTrc("inspector", "even"), value: 0 },
                            { text: qsTrc("inspector", "speeds up"), value: 1 }
                        ]
                        currentIndex: modelData.varies ? -1 : modelData.value + 1
                        displayText: modelData.varies ? qsTrc("inspector", "varies") : modelData.valueText
                        navigation.name: "Playback curve " + modelData.key
                        navigation.panel: root.navigationPanel
                        navigation.row: root.navigationRowStart + 1 + index * 3
                        navigation.accessible.name: modelData.title + " " + modelData.valueText
                        onActivated: function(i, value) { root.model.setPlaybackValue(modelData.key, value) }
                    }
                }

                // Glissandos: where the slide starts, drawn over the note's length, and quick musical positions
                Column {
                    width: parent.width
                    spacing: 4
                    visible: modelData.key === "glissandoStart" && root.startNoteTicks > 0

                    Rectangle {
                        width: parent.width
                        height: 8
                        radius: 2
                        color: ui.theme.buttonColor

                        Rectangle {
                            x: parent.width * Math.min(90, modelData.value) / 100
                            width: parent.width - x
                            height: parent.height
                            radius: 2
                            color: playbackSection.jubelleMagenta
                            opacity: 0.8
                        }
                    }

                    StyledTextLabel {
                        width: parent.width
                        horizontalAlignment: Text.AlignLeft
                        opacity: 0.7
                        text: qsTrc("inspector", "Held note, then the slide (coloured)")
                    }

                    Row {
                        spacing: 4

                        StyledTextLabel {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTrc("inspector", "Start after")
                        }

                        Repeater {
                            // 480 ticks = a quarter note
                            model: [ { text: "1/16", ticks: 120, name: qsTrc("inspector", "a 16th") },
                                     { text: "1/8", ticks: 240, name: qsTrc("inspector", "an 8th") },
                                     { text: "1/4", ticks: 480, name: qsTrc("inspector", "a quarter") },
                                     { text: "1/2", ticks: 960, name: qsTrc("inspector", "a half") } ]

                            delegate: FlatButton {
                                required property var modelData
                                readonly property int percent: Math.round(100 * modelData.ticks / Math.max(1, root.startNoteTicks))
                                visible: percent > 0 && percent <= 90
                                text: modelData.text
                                toolTipTitle: qsTrc("inspector", "Start the slide after %1 (%2% of the note)").arg(modelData.name).arg(percent)
                                onClicked: root.model.setPlaybackValue("glissandoStart", percent)
                            }
                        }
                    }
                }

                JubelleDefaultValue {
                    width: parent.width
                    isSet: modelData.isSet
                    sourceText: modelData.source
                    defaultText: modelData.defaultText
                    highlightColor: playbackSection.jubelleMagenta
                    navigationPanel: root.navigationPanel
                    navigationRow: root.navigationRowStart + 2 + index * 3
                    navigationName: "Playback use default " + modelData.key
                    onUseDefaultRequested: root.model.clearPlaybackValue(modelData.key)
                }
            }
        }
    }
}
