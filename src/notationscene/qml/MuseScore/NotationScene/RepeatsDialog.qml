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

//! Jubelle: Playback → Repeats…  Which repeated sections playback repeats.
//! The repeat signs always stay printed. Nothing changes until Apply.
StyledDialogView {
    id: root

    title: qsTrc("notation", "Repeats")
    objectName: "RepeatsDialog"

    contentWidth: 480
    contentHeight: 480
    margins: 24

    modal: true

    readonly property color jubelleMagenta: "#E600A9"

    RepeatsModel {
        id: repeatsModel
    }

    Component.onCompleted: repeatsModel.load()

    NavigationPanel {
        id: panel
        name: "RepeatsDialogPanel"
        section: root.navigationSection
        direction: NavigationPanel.Vertical
        order: 1
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            text: repeatsModel.repeats.length > 0
                  ? qsTrc("notation", "Untick a repeat to play that section once (with its last ending). The repeat signs stay in the score. D.C. and D.S. keep their own “Play repeats” setting.")
                  : qsTrc("notation", "This score has no repeat barlines.")
        }

        RowLayout {
            visible: repeatsModel.repeats.length > 0
            spacing: 8
            FlatButton {
                text: qsTrc("notation", "Play all")
                navigation.panel: panel
                onClicked: repeatsModel.setAll(true)
            }
            FlatButton {
                text: qsTrc("notation", "Play none")
                navigation.panel: panel
                onClicked: repeatsModel.setAll(false)
            }
        }

        StyledListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
            clip: true
            model: repeatsModel.repeats

            delegate: RowLayout {
                width: ListView.view.width
                spacing: 8

                CheckBox {
                    text: qsTrc("notation", "Play this repeat")
                    checked: modelData.play
                    navigation.panel: panel
                    navigation.order: index + 10
                    navigation.accessible.name: modelData.title + " " + text
                    onClicked: repeatsModel.setPlay(index, !checked)
                }
                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    text: modelData.title + "  ·  " + modelData.detail
                }
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            visible: text !== ""
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            color: root.jubelleMagenta
            text: repeatsModel.resultText
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            NavigationPanel {
                id: buttonsPanel
                name: "RepeatsDialogButtons"
                section: root.navigationSection
                direction: NavigationPanel.Horizontal
                order: 2
            }

            Item { Layout.fillWidth: true }

            FlatButton {
                text: repeatsModel.hasChanges ? qsTrc("global", "Cancel") : qsTrc("global", "Close")
                navigation.panel: buttonsPanel
                navigation.column: 1
                onClicked: root.hide()
            }
            FlatButton {
                text: qsTrc("global", "Apply")
                accentButton: true
                enabled: repeatsModel.hasChanges
                navigation.panel: buttonsPanel
                navigation.column: 2
                onClicked: repeatsModel.apply()
            }
        }
    }
}
