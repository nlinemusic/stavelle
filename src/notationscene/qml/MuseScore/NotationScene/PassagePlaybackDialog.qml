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

//! Jubelle: Articulation Playback for Selection…  A small popup for the
//! highlighted passage. Each change applies at once (one undo step each).
//! Magenta = set for this passage (✕ clears it); grey = inherited.
StyledDialogView {
    id: root

    title: qsTrc("notation", "Articulation Playback for Selection")
    objectName: "PassagePlaybackDialog"

    contentWidth: 460
    contentHeight: Math.min(520, content.implicitHeight)
    margins: 16

    readonly property color jubelleMagenta: "#E600A9"
    readonly property color jubellePurple: "#A855F7"

    PassagePlaybackModel {
        id: passageModel
    }

    Component.onCompleted: passageModel.load()

    NavigationPanel {
        id: panel
        name: "PassagePlaybackPanel"
        section: root.navigationSection
        direction: NavigationPanel.Vertical
        order: 1
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        spacing: 8

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            font: ui.theme.bodyBoldFont
            color: root.jubellePurple
            text: passageModel.isValid ? passageModel.passageTitle
                                       : qsTrc("notation", "Select some bars or notes first (or a playback line).")
        }

        StyledTextLabel {
            Layout.fillWidth: true
            visible: passageModel.isValid && passageModel.rows.length === 0
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            opacity: 0.7
            text: qsTrc("notation", "No articulations in this passage. Tick “Show all” to set others anyway.")
        }

        Repeater {
            model: passageModel.rows

            delegate: RowLayout {
                Layout.fillWidth: true
                spacing: 8

                StyledTextLabel {
                    Layout.preferredWidth: 170
                    horizontalAlignment: Text.AlignLeft
                    text: modelData.group + ": " + modelData.title
                    elide: Text.ElideRight
                }

                IncrementalPropertyControl {
                    visible: modelData.unit !== "curve"
                    Layout.preferredWidth: 110
                    currentValue: modelData.value
                    step: 1
                    decimals: 0
                    minValue: modelData.min
                    maxValue: modelData.max
                    measureUnitsSymbol: modelData.unit === "semitones" ? qsTrc("notation", "st")
                                        : modelData.unit === "% of normal" ? "%" : modelData.unit
                    navigation.panel: panel
                    navigation.order: index * 2
                    navigation.accessible.name: modelData.title
                    onValueEdited: function(newValue) { passageModel.setValue(modelData.key, Math.round(newValue)) }
                }

                StyledDropdown {
                    visible: modelData.unit === "curve"
                    Layout.preferredWidth: 110
                    model: [
                        { text: qsTrc("notation", "Fast, then slows") },
                        { text: qsTrc("notation", "Even") },
                        { text: qsTrc("notation", "Slow, then speeds up") }
                    ]
                    currentIndex: modelData.value + 1
                    navigation.panel: panel
                    navigation.order: index * 2
                    onActivated: function(i, value) { passageModel.setValue(modelData.key, i - 1) }
                }

                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    elide: Text.ElideRight
                    color: modelData.isSet ? root.jubelleMagenta : ui.theme.fontPrimaryColor
                    opacity: modelData.isSet ? 1.0 : 0.6
                    text: modelData.source
                }

                FlatButton {
                    icon: IconCode.CLOSE_X_ROUNDED
                    visible: modelData.isSet
                    toolTipTitle: qsTrc("notation", "Clear: use the inherited value")
                    navigation.panel: panel
                    navigation.order: index * 2 + 1
                    onClicked: passageModel.clearValue(modelData.key)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            CheckBox {
                text: qsTrc("notation", "Show all")
                checked: passageModel.showAll
                enabled: passageModel.isValid
                navigation.panel: panel
                onClicked: passageModel.showAll = !checked
            }

            Item { Layout.fillWidth: true }

            FlatButton {
                text: qsTrc("notation", "Clear passage")
                enabled: passageModel.hasOverrides
                navigation.panel: panel
                onClicked: passageModel.clearAll()
            }

            FlatButton {
                text: qsTrc("global", "Close")
                accentButton: true
                navigation.panel: panel
                onClicked: root.hide()
            }
        }
    }
}
