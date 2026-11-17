/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2025 MuseScore Limited and others
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

import MuseScore.NotationScene
import Muse.Ui
import Muse.UiComponents

StyledFlickable {
    id: root

    contentWidth: Math.max(contentLayout.implicitWidth, root.width)
    contentHeight: contentLayout.implicitHeight

    AccidentalsPageModel {
        id: accidentalsPageModel
    }

    ColumnLayout {
        id: contentLayout
        width: parent.width
        spacing: 12

        StyledGroupBox {
            Layout.fillWidth: true
            title: qsTrc("notation/editstyle/accidentals", "Accidentals")

            StyleSpinboxWithReset {
                styleItem: accidentalsPageModel.bracketedAccidentalPadding
                label: qsTrc("notation/editstyle/accidentals", "Padding inside parentheses:")

                suffix: qsTrc("global", "sp")
                decimals: 3
                step: 0.1
                min: -10.0
                max: 10.0

                labelAreaWidth: -1
                controlAreaWidth: spinBoxWidth
            }
        }

        // Jubelle R8: courtesy accidentals, with presets and a small example for each rule
        StyledGroupBox {
            id: courtesyBox
            Layout.fillWidth: true
            title: qsTrc("notation/editstyle/accidentals", "Courtesy accidentals")

            readonly property var m: accidentalsPageModel
            readonly property bool on: m.courtesyAccidentals.value === true
            readonly property bool parens: m.courtesyAccidentalsParentheses.value === true
            // which preset the values match: 0 off, 1 standard, 2 thorough, 3 your own
            readonly property int preset: {
                if (!on) {
                    return 0
                }
                var octaves = m.courtesyAccidentalsAllOctaves.value === true
                var bars = m.courtesyAccidentalsBars.value
                var other = m.courtesyAccidentalsOtherStaves.value === true
                if (!octaves && bars === 1 && !other && !parens) {
                    return 1
                }
                if (octaves && bars === 2 && other && parens) {
                    return 2
                }
                return 3
            }
            function choose(p) {
                m.courtesyAccidentals.value = p !== 0
                if (p === 1) {
                    m.courtesyAccidentalsAllOctaves.value = false
                    m.courtesyAccidentalsBars.value = 1
                    m.courtesyAccidentalsOtherStaves.value = false
                    m.courtesyAccidentalsParentheses.value = false
                } else if (p === 2) {
                    m.courtesyAccidentalsAllOctaves.value = true
                    m.courtesyAccidentalsBars.value = 2
                    m.courtesyAccidentalsOtherStaves.value = true
                    m.courtesyAccidentalsParentheses.value = true
                }
            }
            // how a courtesy natural looks with the current settings
            readonly property string natural: parens ? "(♮)" : "♮"

            component Example: StyledTextLabel {
                Layout.leftMargin: 28
                horizontalAlignment: Text.AlignLeft
                font.family: "Leland Text"
                font.pixelSize: 15
                opacity: courtesyBox.on ? 0.9 : 0.4
            }

            ColumnLayout {
                width: parent.width
                spacing: 10

                RowLayout {
                    spacing: 8
                    StyledTextLabel { text: qsTrc("notation/editstyle/accidentals", "Preset:") }
                    RoundedRadioButton {
                        text: qsTrc("notation/editstyle/accidentals", "Off")
                        checked: courtesyBox.preset === 0
                        onToggled: courtesyBox.choose(0)
                    }
                    RoundedRadioButton {
                        text: qsTrc("notation/editstyle/accidentals", "Standard")
                        checked: courtesyBox.preset === 1
                        onToggled: courtesyBox.choose(1)
                    }
                    RoundedRadioButton {
                        text: qsTrc("notation/editstyle/accidentals", "Thorough")
                        checked: courtesyBox.preset === 2
                        onToggled: courtesyBox.choose(2)
                    }
                    StyledTextLabel {
                        visible: courtesyBox.preset === 3
                        text: qsTrc("notation/editstyle/accidentals", "(your own settings)")
                        opacity: 0.75
                    }
                }

                StyledTextLabel {
                    Layout.fillWidth: true
                    text: qsTrc("notation/editstyle/accidentals", "Standard: the bar after a change, same octave, this staff. Thorough: two bars, every octave, the instrument’s other staves too, in parentheses. Courtesy accidentals update by themselves as you edit, and you can switch them on or off any time from Format › Courtesy accidentals.")
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.Wrap
                    opacity: 0.75
                }

                CheckBox {
                    text: qsTrc("notation/editstyle/accidentals", "Add courtesy accidentals automatically")
                    checked: courtesyBox.on
                    onClicked: courtesyBox.m.courtesyAccidentals.value = !courtesyBox.on
                }
                Example { text: "| F♯ |  " + courtesyBox.natural + "F |   " + qsTrc("notation/editstyle/accidentals", "the next bar reminds you the F is natural again") }

                CheckBox {
                    text: qsTrc("notation/editstyle/accidentals", "Also in other octaves")
                    enabled: courtesyBox.on
                    checked: courtesyBox.m.courtesyAccidentalsAllOctaves.value === true
                    onClicked: courtesyBox.m.courtesyAccidentalsAllOctaves.value = !courtesyBox.m.courtesyAccidentalsAllOctaves.value
                }
                Example { text: "| F♯⁴ |  " + (courtesyBox.m.courtesyAccidentalsAllOctaves.value === true ? courtesyBox.natural : "") + "F⁵ |   " + qsTrc("notation/editstyle/accidentals", "an F an octave higher") }

                StyleSpinboxWithReset {
                    enabled: courtesyBox.on
                    styleItem: courtesyBox.m.courtesyAccidentalsBars
                    label: qsTrc("notation/editstyle/accidentals", "For how many bars after the change:")
                    decimals: 0
                    step: 1
                    min: 1
                    max: 4

                    labelAreaWidth: -1
                    controlAreaWidth: spinBoxWidth
                }
                Example {
                    text: {
                        var n = courtesyBox.m.courtesyAccidentalsBars.value || 1
                        var bars = "| F♯ "
                        for (var i = 1; i <= 3; ++i) {
                            bars += "| " + (i <= n ? courtesyBox.natural : "") + "F "
                        }
                        return bars + "|"
                    }
                }

                CheckBox {
                    text: qsTrc("notation/editstyle/accidentals", "Also after changes on the instrument’s other staves (for example piano right and left hand, in the same bar too)")
                    enabled: courtesyBox.on
                    checked: courtesyBox.m.courtesyAccidentalsOtherStaves.value === true
                    onClicked: courtesyBox.m.courtesyAccidentalsOtherStaves.value = !courtesyBox.m.courtesyAccidentalsOtherStaves.value
                }
                Example { text: qsTrc("notation/editstyle/accidentals", "right hand F♯, left hand %1").arg((courtesyBox.m.courtesyAccidentalsOtherStaves.value === true ? courtesyBox.natural : "") + "F") }

                CheckBox {
                    text: qsTrc("notation/editstyle/accidentals", "Show courtesy accidentals in parentheses")
                    enabled: courtesyBox.on
                    checked: courtesyBox.parens
                    onClicked: courtesyBox.m.courtesyAccidentalsParentheses.value = !courtesyBox.parens
                }
                Example { text: "(♮)F  " + qsTrc("notation/editstyle/accidentals", "instead of") + "  ♮F" }
            }
        }

        StyledGroupBox {
            Layout.fillWidth: true
            title: qsTrc("notation/editstyle/accidentals", "Multiple accidentals in chords")

            ColumnLayout {
                spacing: 12

                StyleToggleWithImage {
                    id: followNoteOffset
                    styleItem: accidentalsPageModel.accidFollowNoteOffset
                    imageON: "accidentalImages/followOffset-ON.png"
                    imageOFF: "accidentalImages/followOffset-OFF.png"
                    text: qsTrc("notation", "Keep accidentals close to offset notes")
                }

                StyleToggleWithImage {
                    id: keepSecondsTogether
                    styleItem: accidentalsPageModel.keepAccidentalSecondsTogether
                    imageON: "accidentalImages/secondsAdjacent-ON.png"
                    imageOFF: "accidentalImages/secondsAdjacent-OFF.png"
                    text: qsTrc("notation", "Pair accidentals on seconds")
                }

                StyleToggleWithImage {
                    id: alignOctavesAcrossSubChords
                    styleItem: accidentalsPageModel.alignAccidentalOctavesAcrossSubChords
                    imageON: "accidentalImages/octaveSubchord-ON.png"
                    imageOFF: "accidentalImages/octaveSubchord-OFF.png"
                    text: qsTrc("notation", "Align accidentals an octave apart across subchords")
                }

                StyleToggleWithImage {
                    id: alignOffsetOctaveAccidentals
                    styleItem: accidentalsPageModel.alignOffsetOctaveAccidentals
                    imageON: "accidentalImages/octaveOffset-ON.png"
                    imageOFF: "accidentalImages/octaveOffset-OFF.png"
                    text: qsTrc("notation", "Align accidentals on offset octaves")
                }
            }
        }
    }
}
