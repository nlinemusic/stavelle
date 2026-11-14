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

//! Jubelle: Format → Articulation Playback…  How articulations sound, for the
//! whole score or one instrument. Only playback changes; the written music
//! never does. Nothing is changed until Apply is pressed (one undo step).
StyledDialogView {
    id: root

    title: qsTrc("notation", "Articulation Playback")
    objectName: "ArticulationPlaybackDialog"

    contentWidth: 560
    contentHeight: 600
    margins: 24

    modal: true

    readonly property color jubelleMagenta: "#E600A9"
    readonly property color jubellePurple: "#A855F7"

    ArticulationPlaybackModel {
        id: artModel
    }

    Component.onCompleted: artModel.load()

    NavigationPanel {
        id: paramsPanel
        name: "ArticulationPlaybackParams"
        section: root.navigationSection
        direction: NavigationPanel.Vertical
        order: 1
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            StyledTextLabel {
                text: qsTrc("notation", "Settings for:")
                font: ui.theme.bodyBoldFont
            }

            StyledDropdown {
                Layout.fillWidth: true
                model: artModel.scopes
                currentIndex: artModel.currentScope
                navigation.panel: paramsPanel
                navigation.order: 0
                onActivated: function(index, value) { artModel.currentScope = index }
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            opacity: 0.7
            text: artModel.currentScope === 0
                  ? qsTrc("notation", "These values apply to every instrument, unless an instrument has its own. Only playback changes; the written score never does.")
                  : qsTrc("notation", "Values set here apply only to this instrument. Anything not set here uses the whole-score value.")
        }

        StyledListView {
            id: list

            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4
            clip: true

            model: artModel.params

            delegate: Column {
                width: list.width
                spacing: 4

                StyledTextLabel {
                    visible: modelData.showHeader
                    width: parent.width
                    topPadding: index === 0 ? 0 : 12
                    horizontalAlignment: Text.AlignLeft
                    font: ui.theme.bodyBoldFont
                    color: root.jubellePurple
                    text: modelData.group
                }

            RowLayout {
                width: parent.width
                spacing: 8

                StyledTextLabel {
                    Layout.preferredWidth: 190
                    horizontalAlignment: Text.AlignLeft
                    text: modelData.title
                }

                IncrementalPropertyControl {
                    visible: modelData.unit !== "curve"
                    Layout.preferredWidth: 130
                    currentValue: modelData.value
                    step: 1
                    decimals: 0
                    minValue: modelData.min
                    maxValue: modelData.max
                    measureUnitsSymbol: modelData.unit === "semitones" ? qsTrc("notation", "st")
                                        : modelData.unit === "% of normal" ? "%" : modelData.unit
                    navigation.panel: paramsPanel
                    navigation.order: index * 2 + 1
                    navigation.accessible.name: modelData.group + " " + modelData.title
                    onValueEdited: function(newValue) { artModel.setValue(modelData.key, Math.round(newValue)) }
                }

                StyledDropdown {
                    visible: modelData.unit === "curve"
                    Layout.preferredWidth: 130
                    model: [
                        { text: qsTrc("notation", "Fast, then slows") },
                        { text: qsTrc("notation", "Even") },
                        { text: qsTrc("notation", "Slow, then speeds up") }
                    ]
                    currentIndex: modelData.value + 1
                    navigation.panel: paramsPanel
                    navigation.order: index * 2 + 1
                    onActivated: function(i, value) { artModel.setValue(modelData.key, i - 1) }
                }

                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    opacity: modelData.isSet ? 1.0 : 0.6
                    color: modelData.isSet ? root.jubelleMagenta : ui.theme.fontPrimaryColor
                    text: modelData.source
                }

                FlatButton {
                    icon: IconCode.UNDO
                    enabled: modelData.isSet
                    toolTipTitle: qsTrc("notation", "Reset")
                    navigation.panel: paramsPanel
                    navigation.order: index * 2 + 2
                    onClicked: artModel.resetValue(modelData.key)
                }
            }
            }
        }

        // Passages (level 3): hidden playback lines in the score
        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            font: ui.theme.bodyBoldFont
            color: root.jubellePurple
            text: qsTrc("notation", "Passages (%1)").arg(artModel.passages.length)
        }
        StyledTextLabel {
            Layout.fillWidth: true
            visible: artModel.passages.length === 0
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            opacity: 0.7
            text: qsTrc("notation", "None. Select a passage and right-click → Articulation Playback for Selection… to give it its own values.")
        }
        StyledListView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 110)
            visible: artModel.passages.length > 0
            spacing: 4
            clip: true
            model: artModel.passages
            delegate: RowLayout {
                width: ListView.view.width
                spacing: 8
                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    elide: Text.ElideRight
                    text: modelData.title + " — " + modelData.summary
                }
                FlatButton {
                    text: qsTrc("notation", "Go to")
                    navigation.panel: paramsPanel
                    onClicked: artModel.goToPassage(index)
                }
                FlatButton {
                    text: qsTrc("notation", "Remove")
                    navigation.panel: paramsPanel
                    onClicked: artModel.removePassage(index)
                }
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            visible: text !== ""
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            color: root.jubelleMagenta
            text: artModel.resultText
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            NavigationPanel {
                id: buttonsPanel
                name: "ArticulationPlaybackButtons"
                section: root.navigationSection
                direction: NavigationPanel.Horizontal
                order: 2
            }

            FlatButton {
                text: artModel.currentScope === 0 ? qsTrc("notation", "Reset whole score")
                                                  : qsTrc("notation", "Reset this instrument")
                navigation.panel: buttonsPanel
                navigation.column: 1
                onClicked: artModel.resetScope()
            }

            Item { Layout.fillWidth: true }

            FlatButton {
                text: artModel.hasChanges ? qsTrc("global", "Cancel") : qsTrc("global", "Close")
                navigation.panel: buttonsPanel
                navigation.column: 2
                onClicked: root.hide()
            }

            FlatButton {
                text: qsTrc("global", "Apply")
                accentButton: true
                enabled: artModel.hasChanges
                navigation.panel: buttonsPanel
                navigation.column: 3
                onClicked: artModel.apply()
            }
        }
    }
}
