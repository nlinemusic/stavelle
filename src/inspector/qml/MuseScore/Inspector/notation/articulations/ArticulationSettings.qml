/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
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
import MuseScore.Inspector

import "../../common"

Column {
    id: root

    required property ArticulationSettingsModel model

    property NavigationPanel navigationPanel: null
    property int navigationRowStart: 1

    objectName: "ArticulationSettings"

    spacing: 12

    function focusOnFirst() {
        placementSection.focusOnFirst()
    }

    PlacementSection {
        id: placementSection

        enabled: root.model ? root.model.isPlacementAvailable : null

        titleText: qsTrc("inspector", "Placement")
        propertyItem: root.model ? root.model.placement : null

        navigationPanel: root.navigationPanel
        navigationRowStart: root.navigationRowStart

        model: [
            { text: qsTrc("inspector", "Auto"), value: ArticulationTypes.TYPE_AUTO },
            { text: qsTrc("inspector", "Above"), value: ArticulationTypes.TYPE_TOP },
            { text: qsTrc("inspector", "Below"), value: ArticulationTypes.TYPE_BOTTOM }
        ]
    }

    // Jubelle: how the selected articulations sound (playback only)
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
            navigation.row: root.navigationRowStart + 10
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
                        navigation.row: root.navigationRowStart + 11 + index * 2
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
                        navigation.row: root.navigationRowStart + 11 + index * 2
                        navigation.accessible.name: modelData.title + " " + modelData.valueText
                        onActivated: function(i, value) { root.model.setPlaybackValue(modelData.key, value) }
                    }
                }

                JubelleDefaultValue {
                    width: parent.width
                    isSet: modelData.isSet
                    sourceText: modelData.source
                    defaultText: modelData.defaultText
                    highlightColor: playbackSection.jubelleMagenta
                    navigationPanel: root.navigationPanel
                    navigationRow: root.navigationRowStart + 12 + index * 2
                    navigationName: "Playback use default " + modelData.key
                    onUseDefaultRequested: root.model.clearPlaybackValue(modelData.key)
                }
            }
        }
    }
}
