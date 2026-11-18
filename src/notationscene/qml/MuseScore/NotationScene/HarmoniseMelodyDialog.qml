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

//! Jubelle: Harmonise melody (Tools › Arranging › Harmonise melody…), the Big Band Harmony and Orchestration
//! System. Voices the selected melody over the score's chord symbols with the lesson rules, writes the parts
//! as one undo step, plays them, and keeps, discards or compares versions.
StyledDialogView {
    id: root

    title: qsTrc("notation", "Harmonise melody")
    objectName: "HarmoniseMelodyDialog"

    contentWidth: 600
    contentHeight: Math.min(flick.contentHeight, 720)
    margins: 24

    modal: true

    readonly property color jubelleMagenta: "#E600A9"
    readonly property color jubellePurple: "#A855F7"

    HarmoniseMelodyModel {
        id: hm
    }

    Component.onCompleted: hm.load()

    component SectionTitle: StyledTextLabel {
        width: parent ? parent.width : implicitWidth
        font: ui.theme.bodyBoldFont
        color: root.jubellePurple
        horizontalAlignment: Text.AlignLeft
    }

    component Hint: StyledTextLabel {
        width: parent ? parent.width : implicitWidth
        horizontalAlignment: Text.AlignLeft
        wrapMode: Text.Wrap
        opacity: 0.7
    }

    //! a technique chooser: label, dropdown, and the lesson's note on the chosen technique
    component TechniqueRow: Column {
        id: row
        property string label
        property var options: []
        property int value: 0
        property int navRow: 0
        property NavigationPanel navPanel: null
        signal chosen(int v)

        width: parent ? parent.width : 0
        spacing: 4

        RowLayout {
            width: parent.width
            spacing: 12

            StyledTextLabel {
                Layout.preferredWidth: 110
                text: row.label
                horizontalAlignment: Text.AlignLeft
            }

            StyledDropdown {
                Layout.fillWidth: true
                model: row.options
                textRole: "text"
                valueRole: "value"
                currentIndex: indexOfValue(row.value)
                navigation.panel: row.navPanel
                navigation.row: row.navRow
                onActivated: function(index, value) { row.chosen(value) }
            }
        }

        Hint {
            leftPadding: 122
            visible: text !== ""
            text: {
                for (var i = 0; i < row.options.length; ++i) {
                    if (row.options[i].value === row.value) {
                        return row.options[i].hint
                    }
                }
                return ""
            }
        }
    }

    NavigationPanel {
        id: optionsPanel
        name: "HarmoniseOptions"
        section: root.navigationSection
        direction: NavigationPanel.Vertical
        order: 1
    }

    StyledFlickable {
        id: flick
        anchors.fill: parent
        contentHeight: content.implicitHeight

        Column {
            id: content

            width: parent.width
            spacing: 16

            // What will be harmonised
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
                        text: hm.scopeTitle
                        font: ui.theme.bodyBoldFont
                        horizontalAlignment: Text.AlignLeft
                    }

                    StyledTextLabel {
                        width: parent.width
                        text: hm.scopeDetail
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.Wrap
                        opacity: 0.8
                    }

                    StyledTextLabel {
                        width: parent.width
                        visible: text !== ""
                        text: hm.sectionsText
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.Wrap
                        opacity: 0.8
                    }
                }
            }

            // Sections
            Column {
                width: parent.width
                spacing: 10

                SectionTitle { text: qsTrc("notation", "Sections") }

                TechniqueRow {
                    label: qsTrc("notation", "Trumpets")
                    options: hm.trumpetTechniques
                    value: hm.trumpets
                    navRow: 1
                    navPanel: optionsPanel
                    onChosen: function(v) { hm.trumpets = v }
                }

                TechniqueRow {
                    label: qsTrc("notation", "Trombones")
                    options: hm.tromboneTechniques
                    value: hm.trombones
                    navRow: 2
                    navPanel: optionsPanel
                    onChosen: function(v) { hm.trombones = v }
                }

                TechniqueRow {
                    label: qsTrc("notation", "Saxophones")
                    options: hm.saxTechniques
                    value: hm.saxes
                    navRow: 3
                    navPanel: optionsPanel
                    onChosen: function(v) { hm.saxes = v }
                }

                StyledTextLabel {
                    width: parent.width
                    visible: hm.saxWarning !== ""
                    text: hm.saxWarning
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.Wrap
                    color: root.jubelleMagenta
                }
            }

            // Harmony
            Column {
                width: parent.width
                spacing: 10

                SectionTitle { text: qsTrc("notation", "Harmony") }

                TechniqueRow {
                    label: qsTrc("notation", "Passing notes")
                    options: hm.nctTools
                    value: hm.nctTool
                    navRow: 4
                    navPanel: optionsPanel
                    onChosen: function(v) { hm.nctTool = v }
                }

                CheckBox {
                    text: qsTrc("notation", "Alterations on a V chord that resolves (♭9 and ♯5 preferred, as the melody allows)")
                    checked: hm.addAlterations
                    navigation.panel: optionsPanel
                    navigation.row: 5
                    onClicked: hm.addAlterations = !checked
                }

                CheckBox {
                    text: qsTrc("notation", "Half-step slide: replace the V of a ii–V–I with the dominant a tritone away")
                    checked: hm.halfStepSlide
                    navigation.panel: optionsPanel
                    navigation.row: 6
                    onClicked: hm.halfStepSlide = !checked
                }

                CheckBox {
                    text: qsTrc("notation", "Write changed chord symbols on the piano staff too (else guitar, else the top staff)")
                    checked: hm.writeChordSymbols
                    navigation.panel: optionsPanel
                    navigation.row: 7
                    onClicked: hm.writeChordSymbols = !checked
                }

                Hint {
                    text: qsTrc("notation", "A chord symbol that changes is always updated where it already is, so the rhythm section reads what the horns play.")
                }
            }

            // Existing notes
            Column {
                width: parent.width
                spacing: 8

                SectionTitle { text: qsTrc("notation", "Where a staff already has notes in this passage") }

                RoundedRadioButton {
                    text: qsTrc("notation", "Leave that staff alone")
                    checked: hm.keepExisting
                    navigation.panel: optionsPanel
                    navigation.row: 8
                    onToggled: hm.keepExisting = true
                }

                RoundedRadioButton {
                    text: qsTrc("notation", "Replace its notes")
                    checked: !hm.keepExisting
                    navigation.panel: optionsPanel
                    navigation.row: 9
                    onToggled: hm.keepExisting = false
                }
            }

            // Compare
            Column {
                width: parent.width
                spacing: 8
                visible: hm.versions.length > 0

                SectionTitle { text: qsTrc("notation", "Compare") }

                Repeater {
                    model: hm.versions

                    delegate: RowLayout {
                        required property var modelData
                        required property int index
                        width: content.width
                        spacing: 8

                        FlatButton {
                            text: modelData.name
                            accentButton: hm.currentVersion === index
                            onClicked: hm.showVersion(index)
                        }

                        StyledTextLabel {
                            Layout.fillWidth: true
                            text: modelData.detail
                            horizontalAlignment: Text.AlignLeft
                            elide: Text.ElideRight
                        }

                        FlatButton {
                            icon: IconCode.DELETE_TANK
                            toolTipTitle: qsTrc("notation", "Remove this version")
                            onClicked: hm.removeVersion(index)
                        }
                    }
                }
            }

            StyledTextLabel {
                width: parent.width
                visible: hm.resultText !== ""
                text: hm.resultText
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.Wrap
                color: root.jubelleMagenta
            }

            RowLayout {
                width: parent.width
                spacing: 8

                NavigationPanel {
                    id: buttonsPanel
                    name: "HarmoniseButtons"
                    section: root.navigationSection
                    direction: NavigationPanel.Horizontal
                    order: 2
                }

                FlatButton {
                    text: qsTrc("notation", "Save as version")
                    enabled: hm.canRun && hm.versions.length < 4
                    toolTipTitle: qsTrc("notation", "Keep these settings to compare with others (up to four)")
                    navigation.panel: buttonsPanel
                    navigation.column: 1
                    onClicked: hm.addVersion()
                }

                Item { Layout.fillWidth: true }

                FlatButton {
                    text: qsTrc("notation", "Discard")
                    visible: hm.applied
                    navigation.panel: buttonsPanel
                    navigation.column: 2
                    onClicked: hm.discard()
                }

                FlatButton {
                    text: hm.applied ? qsTrc("notation", "Regenerate") : qsTrc("notation", "Preview")
                    enabled: hm.canRun
                    toolTipTitle: qsTrc("notation", "Write the parts (one Undo removes them) and play the passage")
                    navigation.panel: buttonsPanel
                    navigation.column: 3
                    onClicked: hm.preview()
                }

                FlatButton {
                    text: hm.applied ? qsTrc("notation", "Keep") : qsTrc("global", "Close")
                    accentButton: hm.applied
                    navigation.panel: buttonsPanel
                    navigation.column: 4
                    onClicked: {
                        hm.keep()
                        root.hide()
                    }
                }
            }
        }
    }
}
