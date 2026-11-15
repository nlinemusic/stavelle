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

//! Jubelle G2: Tools → Condensing → Condensed Score…
//! Makes or updates a separate condensed copy, only when you press the button.
//! The score itself never changes.
StyledDialogView {
    id: root

    title: qsTrc("notation", "Condensed Score")
    objectName: "CondensedScoreDialog"

    contentWidth: 540
    contentHeight: Math.min(600, content.implicitHeight)
    margins: 20

    readonly property color jubelleMagenta: "#E600A9"
    readonly property color jubellePurple: "#A855F7"

    CondensedScoreModel {
        id: csModel
    }

    Component.onCompleted: csModel.load()

    NavigationPanel {
        id: panel
        name: "CondensedScorePanel"
        section: root.navigationSection
        direction: NavigationPanel.Vertical
        order: 1
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        spacing: 10

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            text: qsTrc("notation", "A condensed score is a separate copy of this full score in which pairs of players of the same instrument (Flute 1 and 2, Trumpet 1 and 2…) share one staff where the music allows: “a2” in unison, chords on one stem, two voices for different rhythms, and “1.” or “2.” when one plays alone. Where they need their own staves, they get them back on that system only.")
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            font: ui.theme.bodyBoldFont
            text: qsTrc("notation", "Your score is never changed. The copy is only made or updated when you press the button below.")
        }

        // The on/off setting (the same as Tools → Condensing → Enable Condensed Scores)
        CheckBox {
            text: qsTrc("notation", "Condensed scores are on")
            checked: csModel.condensingEnabled
            navigation.panel: panel
            navigation.order: 1
            onClicked: csModel.toggleEnabled()
        }

        StyledTextLabel {
            Layout.fillWidth: true
            visible: !csModel.condensingEnabled
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            opacity: 0.75
            text: qsTrc("notation", "Condensed scores are off, as they are until you switch them on. Nothing is condensed while they are off.")
        }

        // ---- In a condensed copy ----
        StyledTextLabel {
            Layout.fillWidth: true
            visible: csModel.isCopy
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            color: root.jubellePurple
            text: qsTrc("notation", "This is a condensed copy. To update it, save and close it, open the original score and use this command there. To choose how some bars are condensed, highlight them on a player's staff and right-click → Condensing; your choices are kept here and used at the next update.")
        }

        // ---- In the original ----
        StyledTextLabel {
            Layout.fillWidth: true
            visible: !csModel.isCopy && csModel.pairs.length > 0
            horizontalAlignment: Text.AlignLeft
            text: qsTrc("notation", "Players who can share a staff:")
        }

        Repeater {
            model: csModel.isCopy ? [] : csModel.pairs
            delegate: StyledTextLabel {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                horizontalAlignment: Text.AlignLeft
                text: "• " + modelData
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            visible: !csModel.isCopy && csModel.copyFileName !== ""
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            text: qsTrc("notation", "Saved as “%1” in the score’s folder (%2).").arg(csModel.copyFileName).arg(csModel.copyFolder)
        }

        StyledTextLabel {
            Layout.fillWidth: true
            visible: csModel.overrides.length > 0
            horizontalAlignment: Text.AlignLeft
            text: qsTrc("notation", "Your choices in the copy:")
        }

        Repeater {
            model: csModel.overrides
            delegate: StyledTextLabel {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                horizontalAlignment: Text.AlignLeft
                text: "• " + modelData
            }
        }

        // Updating replaces the copy: say so, and ask for a tick first
        StyledTextLabel {
            Layout.fillWidth: true
            visible: !csModel.isCopy && csModel.copyExists
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            color: root.jubelleMagenta
            text: qsTrc("notation", "Updating replaces everything in the condensed copy, including any edits you made in it. Only your condensing choices for bars are kept. Your score is not changed.")
        }

        CheckBox {
            id: replaceConfirm
            visible: !csModel.isCopy && csModel.copyExists
            text: qsTrc("notation", "Replace the copy’s contents")
            navigation.panel: panel
            navigation.order: 2
            onClicked: checked = !checked
        }

        StyledTextLabel {
            Layout.fillWidth: true
            visible: csModel.problem !== ""
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            color: root.jubelleMagenta
            text: csModel.problem
        }

        StyledTextLabel {
            Layout.fillWidth: true
            visible: csModel.resultText !== ""
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            color: root.jubellePurple
            text: csModel.resultText
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Item { Layout.fillWidth: true }

            FlatButton {
                visible: !csModel.isCopy && csModel.copyExists
                text: qsTrc("notation", "Open condensed score")
                navigation.panel: panel
                navigation.order: 4
                onClicked: {
                    csModel.openCopy()
                    root.hide()
                }
            }

            FlatButton {
                visible: !csModel.isCopy
                text: csModel.copyExists ? qsTrc("notation", "Update condensed score") : qsTrc("notation", "Create condensed score")
                accentButton: true
                enabled: csModel.condensingEnabled && csModel.problem === "" && csModel.copyFileName !== ""
                         && (!csModel.copyExists || replaceConfirm.checked)
                navigation.panel: panel
                navigation.order: 3
                onClicked: {
                    csModel.createOrUpdate()
                    replaceConfirm.checked = false
                }
            }

            FlatButton {
                text: qsTrc("global", "Close")
                navigation.panel: panel
                navigation.order: 5
                onClicked: root.hide()
            }
        }
    }
}
