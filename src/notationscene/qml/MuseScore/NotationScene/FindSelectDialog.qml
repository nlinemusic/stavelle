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

//! Jubelle G5: Select › Find and Select…  Choose what to look for and where;
//! the count updates as you go; Select selects every match. Finding never
//! changes the score. Saved filters are kept in Jubelle's settings.
StyledDialogView {
    id: root

    title: qsTrc("notation/findselect", "Find and Select")
    objectName: "FindSelectDialog"

    contentWidth: 640
    contentHeight: 620
    margins: 24

    FindSelectModel {
        id: fs
    }

    Component.onCompleted: fs.load()

    component Heading: StyledTextLabel {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignLeft
        font: ui.theme.bodyBoldFont
        color: ui.theme.isDark ? "#C084FC" : "#7E22CE"
    }

    StyledFlickable {
        id: flick
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: footer.top
        anchors.bottomMargin: 12
        contentHeight: content.implicitHeight
        clip: true

        ColumnLayout {
            id: content
            width: flick.width
            spacing: 10

            Heading { text: qsTrc("notation/findselect", "Find") }
            Flow {
                Layout.fillWidth: true
                spacing: 12
                Repeater {
                    model: fs.kindOptions
                    CheckBox {
                        required property var modelData
                        text: modelData.title
                        checked: fs.selectedKinds.indexOf(modelData.id) >= 0
                        onClicked: fs.setKind(modelData.id, !checked)
                    }
                }
            }

            Heading { text: qsTrc("notation/findselect", "In") }
            RowLayout {
                spacing: 12
                CheckBox {
                    text: qsTrc("notation/findselect", "All instruments")
                    checked: fs.selectedStaves.length === 0
                    onClicked: fs.setAllStaves()
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 12
                Repeater {
                    model: fs.staffOptions
                    CheckBox {
                        required property var modelData
                        text: modelData.title
                        checked: fs.selectedStaves.indexOf(modelData.staff) >= 0
                        onClicked: fs.setStaff(modelData.staff, !checked)
                    }
                }
            }
            RowLayout {
                spacing: 12
                StyledTextLabel { text: qsTrc("notation/findselect", "Voices:") }
                Repeater {
                    model: 4
                    CheckBox {
                        required property int index
                        text: String(index + 1)
                        checked: (fs.voices & (1 << index)) !== 0
                        onClicked: fs.setVoice(index, !checked)
                    }
                }
            }

            Heading { text: qsTrc("notation/findselect", "Notes") }
            RowLayout {
                spacing: 8
                CheckBox {
                    text: qsTrc("notation/findselect", "Pitch from")
                    checked: fs.usePitch
                    onClicked: fs.setUsePitch(!checked)
                }
                IncrementalPropertyControl {
                    Layout.preferredWidth: 80
                    enabled: fs.usePitch
                    currentValue: fs.lowestPitch
                    decimals: 0
                    step: 1
                    minValue: 0
                    maxValue: 127
                    onValueEditingFinished: function(v) { fs.setLowestPitch(v) }
                }
                StyledTextLabel { text: fs.lowestPitchName; opacity: 0.75 }
                StyledTextLabel { text: qsTrc("notation/findselect", "to") }
                IncrementalPropertyControl {
                    Layout.preferredWidth: 80
                    enabled: fs.usePitch
                    currentValue: fs.highestPitch
                    decimals: 0
                    step: 1
                    minValue: 0
                    maxValue: 127
                    onValueEditingFinished: function(v) { fs.setHighestPitch(v) }
                }
                StyledTextLabel { text: fs.highestPitchName; opacity: 0.75 }
            }
            CheckBox {
                Layout.leftMargin: 28
                enabled: fs.usePitch
                text: qsTrc("notation/findselect", "Written pitch (as the player reads it), not sounding pitch")
                checked: fs.writtenPitch
                onClicked: fs.setWrittenPitch(!checked)
            }
            RowLayout {
                spacing: 8
                CheckBox {
                    text: qsTrc("notation/findselect", "Note value from")
                    checked: fs.useLength
                    onClicked: fs.setUseLength(!checked)
                }
                StyledDropdown {
                    Layout.preferredWidth: 110
                    enabled: fs.useLength
                    model: fs.lengthOptions
                    currentIndex: fs.shortestIndex
                    onActivated: function(i, v) { fs.setShortestIndex(v) }
                }
                StyledTextLabel { text: qsTrc("notation/findselect", "to") }
                StyledDropdown {
                    Layout.preferredWidth: 110
                    enabled: fs.useLength
                    model: fs.lengthOptions
                    currentIndex: fs.longestIndex
                    onActivated: function(i, v) { fs.setLongestIndex(v) }
                }
            }
            RowLayout {
                spacing: 8
                StyledTextLabel { text: qsTrc("notation/findselect", "With articulation:") }
                StyledDropdown {
                    Layout.preferredWidth: 160
                    model: fs.articulationOptions
                    currentIndex: indexOfValue(fs.articulation)
                    onActivated: function(i, v) { fs.setArticulation(v) }
                }
            }
            CheckBox {
                text: qsTrc("notation/findselect", "Skip tied notes after the first")
                checked: fs.skipTied
                onClicked: fs.setSkipTied(!checked)
            }

            Heading { text: qsTrc("notation/findselect", "Where") }
            RowLayout {
                spacing: 12
                FlatRadioButton {
                    text: qsTrc("notation/findselect", "Whole score")
                    checked: fs.rangeMode === 0
                    onToggled: fs.setRangeMode(0)
                }
                FlatRadioButton {
                    text: qsTrc("notation/findselect", "Bars")
                    checked: fs.rangeMode === 1
                    onToggled: fs.setRangeMode(1)
                }
                FlatRadioButton {
                    text: qsTrc("notation/findselect", "Current selection")
                    checked: fs.rangeMode === 2
                    onToggled: fs.setRangeMode(2)
                }
            }
            RowLayout {
                visible: fs.rangeMode === 1
                spacing: 8
                IncrementalPropertyControl {
                    Layout.preferredWidth: 80
                    currentValue: fs.fromBar
                    decimals: 0
                    step: 1
                    minValue: 1
                    maxValue: Math.max(1, fs.barCount)
                    onValueEditingFinished: function(v) { fs.setFromBar(v) }
                }
                StyledTextLabel { text: qsTrc("notation/findselect", "to") }
                IncrementalPropertyControl {
                    Layout.preferredWidth: 80
                    currentValue: fs.toBar
                    decimals: 0
                    step: 1
                    minValue: 1
                    maxValue: Math.max(1, fs.barCount)
                    onValueEditingFinished: function(v) { fs.setToBar(v) }
                }
            }

            Heading { text: qsTrc("notation/findselect", "Saved filters") }
            RowLayout {
                spacing: 8
                StyledDropdown {
                    id: savedList
                    Layout.preferredWidth: 220
                    model: fs.savedFilters.map(function(n) { return { "text": n, "value": n } })
                    currentIndex: -1
                    displayText: fs.savedFilters.length > 0 ? qsTrc("notation/findselect", "Load a filter…") : qsTrc("notation/findselect", "No saved filters")
                    enabled: fs.savedFilters.length > 0
                    onActivated: function(i, v) { fs.loadFilter(v); filterName.currentText = v }
                }
                TextInputField {
                    id: filterName
                    Layout.preferredWidth: 180
                    hint: qsTrc("notation/findselect", "Name")
                    onTextChanged: function(t) { currentText = t }
                }
                FlatButton {
                    text: qsTrc("notation/findselect", "Save")
                    enabled: filterName.currentText.trim() !== ""
                    onClicked: fs.saveFilter(filterName.currentText)
                }
                FlatButton {
                    text: qsTrc("notation/findselect", "Delete")
                    enabled: fs.savedFilters.indexOf(filterName.currentText) >= 0
                    onClicked: fs.deleteFilter(filterName.currentText)
                }
            }
        }
    }

    RowLayout {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 8

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            font: ui.theme.bodyBoldFont
            text: qsTrc("notation/findselect", "%n match(es)", "", fs.matchCount)
        }
        FlatButton {
            text: qsTrc("global", "Close")
            onClicked: root.hide()
        }
        FlatButton {
            text: qsTrc("notation/findselect", "Select")
            accentButton: true
            enabled: fs.matchCount > 0
            onClicked: {
                fs.selectMatches()
                root.hide()
            }
        }
    }
}
