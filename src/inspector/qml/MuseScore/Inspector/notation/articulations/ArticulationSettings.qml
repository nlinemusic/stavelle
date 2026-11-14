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

                    FlatButton {
                        icon: IconCode.UNDO
                        enabled: modelData.isSet
                        toolTipTitle: qsTrc("inspector", "Clear: use the inherited value")
                        navigation.name: "Playback clear " + modelData.key
                        navigation.panel: root.navigationPanel
                        navigation.row: root.navigationRowStart + 12 + index * 2
                        onClicked: root.model.clearPlaybackValue(modelData.key)
                    }

                    StyledTextLabel {
                        width: parent.width / 2 - 40
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideRight
                        color: modelData.isSet ? playbackSection.jubelleMagenta : ui.theme.fontPrimaryColor
                        opacity: modelData.isSet ? 1.0 : 0.6
                        text: modelData.source
                    }
                }
            }
        }
    }
}
