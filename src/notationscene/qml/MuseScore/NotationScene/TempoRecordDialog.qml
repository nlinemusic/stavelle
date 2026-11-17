/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.NotationScene

//! Jubelle G6: Playback › Record tempo…  Tap the beat while the score plays.
StyledDialogView {
    id: root

    title: qsTrc("notation/temporecord", "Record tempo")
    objectName: "TempoRecordDialog"

    contentWidth: 520
    contentHeight: 470
    margins: 24

    TempoRecordModel {
        id: tr
    }

    Component.onCompleted: tr.load()

    // any key taps while recording (Esc still closes)
    Keys.onPressed: function(event) {
        if (tr.recording && event.key !== Qt.Key_Escape && !event.isAutoRepeat) {
            tr.tap()
            event.accepted = true
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            text: qsTrc("notation/temporecord", "Press Start, then tap the beat. Your first tap starts the music. Tap with the big button, any key, or a MIDI key or pedal. The tempo is stored as hidden tempo markings: nothing is printed and no notes change.")
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            opacity: 0.75
            text: tr.startText
        }

        RowLayout {
            spacing: 8
            StyledTextLabel { text: qsTrc("notation/temporecord", "Each tap is:") }
            StyledDropdown {
                Layout.preferredWidth: 280
                enabled: !tr.recording
                model: tr.beatOptions
                currentIndex: tr.beatIndex
                onActivated: function(i, v) { tr.setBeatIndex(v) }
            }
        }

        RowLayout {
            spacing: 8
            StyledTextLabel { text: qsTrc("notation/temporecord", "Smoothing:") }
            StyledSlider {
                Layout.preferredWidth: 200
                enabled: !tr.recording
                from: 0
                to: 90
                stepSize: 5
                value: tr.smoothingPercent
                onMoved: tr.setSmoothingPercent(value)
            }
            StyledTextLabel {
                opacity: 0.75
                text: tr.smoothingPercent < 15 ? qsTrc("notation/temporecord", "every tap as played")
                                               : tr.smoothingPercent < 50 ? qsTrc("notation/temporecord", "natural")
                                                                          : qsTrc("notation/temporecord", "very smooth")
            }
        }

        CheckBox {
            enabled: !tr.recording
            text: qsTrc("notation/temporecord", "Follow me: the music keeps up with my taps while I tap")
            checked: tr.followMe
            onClicked: tr.setFollowMe(!checked)
        }
        CheckBox {
            enabled: !tr.recording
            text: qsTrc("notation/temporecord", "Also tap with a MIDI key or sustain pedal")
            checked: tr.midiTap
            onClicked: tr.setMidiTap(!checked)
        }

        FlatButton {
            id: tapButton
            Layout.fillWidth: true
            Layout.preferredHeight: 96
            accentButton: tr.recording
            enabled: tr.recording
            text: !tr.recording ? qsTrc("notation/temporecord", "Tap")
                                : tr.tapCount === 0 ? qsTrc("notation/temporecord", "Tap to start the music")
                                                    : qsTrc("notation/temporecord", "Tap  ·  %1 taps  ·  ♩ = %2").arg(tr.tapCount).arg(tr.liveBpm)
            onClicked: tr.tap()
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            visible: text !== ""
            text: tr.resultText
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            FlatButton {
                text: qsTrc("notation/temporecord", "Remove recorded tempo")
                enabled: !tr.recording && tr.recordedPoints > 0
                toolTipTitle: text
                toolTipDescription: qsTrc("notation/temporecord", "From the selected passage, or the whole score if nothing is selected. Your own tempo markings stay.")
                onClicked: tr.clearRecorded()
            }
            Item { Layout.fillWidth: true }
            FlatButton {
                text: qsTrc("global", "Close")
                onClicked: {
                    tr.cancel()
                    root.hide()
                }
            }
            FlatButton {
                visible: tr.recording
                text: qsTrc("global", "Cancel")
                onClicked: tr.cancel()
            }
            FlatButton {
                accentButton: !tr.recording
                text: tr.recording ? qsTrc("notation/temporecord", "Finish") : qsTrc("notation/temporecord", "Start")
                onClicked: {
                    if (tr.recording) {
                        tr.finish()
                    } else {
                        tr.start()
                        tapButton.forceActiveFocus()
                    }
                }
            }
        }
    }
}
