/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 * Music Composition & Notation
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
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.NotationScene

//! Jubelle: Chord Identifier (Tools → Chord Identifier…). Names the chords in
//! the selection, or in the whole score when nothing is selected, as chord
//! symbols and/or Roman numerals, as one undoable step.
StyledDialogView {
    id: root

    title: qsTrc("notation", "Chord Identifier")
    objectName: "ChordIdentifierDialog"

    contentWidth: 520
    contentHeight: content.implicitHeight
    margins: 24

    modal: true

    readonly property color jubelleMagenta: "#E600A9"
    readonly property color jubellePurple: "#A855F7"

    ChordIdentifierModel {
        id: chordModel
    }

    Component.onCompleted: chordModel.load()

    component SectionTitle: StyledTextLabel {
        width: parent ? parent.width : implicitWidth
        font: ui.theme.bodyBoldFont
        color: root.jubellePurple
        horizontalAlignment: Text.AlignLeft
    }

    component Hint: StyledTextLabel {
        width: parent ? parent.width : implicitWidth
        leftPadding: 28
        horizontalAlignment: Text.AlignLeft
        wrapMode: Text.Wrap
        opacity: 0.7
    }

    Column {
        id: content

        width: parent.width
        spacing: 16

        // What will be analysed
        Rectangle {
            width: parent.width
            height: scopeColumn.implicitHeight + 24
            radius: 8
            color: ui.theme.backgroundSecondaryColor
            border.color: ui.theme.strokeColor
            border.width: 1

            Rectangle {
                width: 4
                height: parent.height - 16
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                radius: 2
                color: root.jubelleMagenta
            }

            Column {
                id: scopeColumn

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 24
                anchors.rightMargin: 12
                spacing: 4

                StyledTextLabel {
                    width: parent.width
                    text: qsTrc("notation", "Will analyse: %1").arg(chordModel.scopeTitle)
                    font: ui.theme.bodyBoldFont
                    horizontalAlignment: Text.AlignLeft
                }

                StyledTextLabel {
                    width: parent.width
                    text: chordModel.scopeDetail
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.Wrap
                    opacity: 0.8
                }
            }
        }

        NavigationPanel {
            id: optionsPanel
            name: "ChordIdentifierOptions"
            section: root.navigationSection
            direction: NavigationPanel.Vertical
            order: 1
        }

        // Write as
        Column {
            width: parent.width
            spacing: 8

            SectionTitle { text: qsTrc("notation", "Write the chords as") }

            CheckBox {
                text: qsTrc("notation", "Chord symbols (above the top staff), e.g. Cmaj7, G7/B")
                checked: chordModel.chordSymbols
                navigation.panel: optionsPanel
                navigation.row: 1
                onClicked: chordModel.chordSymbols = !checked
            }

            CheckBox {
                x: 28
                enabled: chordModel.chordSymbols
                text: qsTrc("notation", "Show the bass note after a slash for inversions (C/E)")
                checked: chordModel.slashBass
                navigation.panel: optionsPanel
                navigation.row: 2
                onClicked: chordModel.slashBass = !checked
            }

            CheckBox {
                text: qsTrc("notation", "Roman numerals (below the bottom staff), e.g. I, ii7, V65")
                checked: chordModel.romanNumerals
                navigation.panel: optionsPanel
                navigation.row: 3
                onClicked: chordModel.romanNumerals = !checked
            }

            CheckBox {
                x: 28
                enabled: chordModel.romanNumerals
                text: qsTrc("notation", "Figured-bass inversions (I6, I64, V65, V43, V42)")
                checked: chordModel.figuredBass
                navigation.panel: optionsPanel
                navigation.row: 4
                onClicked: chordModel.figuredBass = !checked
            }

            Hint {
                visible: chordModel.romanNumerals
                text: qsTrc("notation", "Numerals follow the key signature at each point; a key signature marked minor is read as minor.")
            }
        }

        // Existing symbols and repeats
        Column {
            width: parent.width
            spacing: 8

            SectionTitle { text: qsTrc("notation", "Where a symbol is already there") }

            RoundedRadioButton {
                text: qsTrc("notation", "Keep it, and only add symbols where there are none")
                checked: !chordModel.replaceExisting
                navigation.panel: optionsPanel
                navigation.row: 5
                onToggled: chordModel.replaceExisting = false
            }

            RoundedRadioButton {
                text: qsTrc("notation", "Replace it with the identified chord")
                checked: chordModel.replaceExisting
                navigation.panel: optionsPanel
                navigation.row: 6
                onToggled: chordModel.replaceExisting = true
            }

            CheckBox {
                text: qsTrc("notation", "Only add a symbol when the chord changes")
                checked: chordModel.onlyWhenChordChanges
                navigation.panel: optionsPanel
                navigation.row: 7
                onClicked: chordModel.onlyWhenChordChanges = !checked
            }

            Hint {
                text: qsTrc("notation", "Running the identifier again never adds a second symbol in the same place.")
            }
        }

        // Highlighting
        Column {
            width: parent.width
            spacing: 8

            SectionTitle { text: qsTrc("notation", "Highlight chord tones") }

            CheckBox {
                text: qsTrc("notation", "Colour each note by its role in the chord")
                checked: chordModel.highlight
                navigation.panel: optionsPanel
                navigation.row: 8
                onClicked: chordModel.highlight = !checked
            }

            Flow {
                width: parent.width
                leftPadding: 28
                spacing: 14
                opacity: chordModel.highlight ? 1.0 : 0.5

                Repeater {
                    model: chordModel.highlightLegend

                    delegate: Row {
                        required property var modelData
                        spacing: 6

                        Rectangle {
                            width: 12
                            height: 12
                            radius: 6
                            anchors.verticalCenter: parent.verticalCenter
                            color: modelData.color
                        }

                        StyledTextLabel {
                            text: modelData.label
                        }
                    }
                }
            }

            Hint {
                text: qsTrc("notation", "Colours you chose yourself are never changed unless this is on. “Remove highlighting” resets only these colours.")
            }
        }

        StyledTextLabel {
            width: parent.width
            visible: chordModel.resultText !== ""
            text: chordModel.resultText
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            color: root.jubelleMagenta
        }

        RowLayout {
            width: parent.width
            spacing: 8

            NavigationPanel {
                id: buttonsPanel
                name: "ChordIdentifierButtons"
                section: root.navigationSection
                direction: NavigationPanel.Horizontal
                order: 2
            }

            FlatButton {
                text: qsTrc("notation", "Remove highlighting")
                navigation.panel: buttonsPanel
                navigation.column: 1
                onClicked: chordModel.removeHighlight()
            }

            Item { Layout.fillWidth: true }

            FlatButton {
                text: chordModel.resultText !== "" ? qsTrc("global", "Close") : qsTrc("global", "Cancel")
                navigation.panel: buttonsPanel
                navigation.column: 2
                onClicked: root.hide()
            }

            FlatButton {
                text: qsTrc("notation", "Identify chords")
                accentButton: true
                enabled: chordModel.canRun
                navigation.panel: buttonsPanel
                navigation.column: 3
                onClicked: chordModel.run()
            }
        }
    }
}
