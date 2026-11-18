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
import MuseScore.AppShell

import "LearningContent.js" as Content

//! Jubelle G10: the Learning Center. Guides to Jubelle's own features, tutorials,
//! and an instrument reference (ranges and transpositions) read from the program's
//! instrument data. Everything works offline.
FocusScope {
    id: root

    readonly property color jubellePurple: "#A855F7"
    readonly property string rangesSection: "Instrument ranges and transposition"
    readonly property string shortcutsSection: "Keyboard shortcuts"

    property string currentSection: "Start here"
    property string searchText: ""
    property var openTopic: null

    readonly property bool searching: searchText.trim() !== ""
    readonly property var shownTopics: Content.topicsIn(currentSection, searchText.trim())

    LearningCenterModel {
        id: lc
    }

    Component.onCompleted: lc.load()

    NavigationSection {
        id: navSec
        name: "LearningCenter"
        enabled: root.enabled && root.visible
        order: 3
        onActiveChanged: function(active) {
            if (active) {
                root.forceActiveFocus()
            }
        }
    }

    NavigationPanel {
        id: navPanel
        name: "LearningCenterPanel"
        section: navSec
        order: 1
    }

    Rectangle {
        anchors.fill: parent
        color: ui.theme.backgroundSecondaryColor
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 24

        // ---------------- sections ----------------
        ColumnLayout {
            Layout.preferredWidth: 250
            Layout.maximumWidth: 250
            Layout.fillHeight: true
            spacing: 6

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                text: qsTrc("appshell", "Learning Center")
                font: ui.theme.titleBoldFont
            }

            SearchField {
                id: searchField
                Layout.fillWidth: true
                Layout.topMargin: 6
                Layout.bottomMargin: 6
                hint: qsTrc("appshell", "Search the guides")
                onSearchTextChanged: {
                    root.searchText = searchField.searchText
                    root.openTopic = null
                }
            }

            Repeater {
                model: Content.sections()

                FlatButton {
                    id: sectionButton
                    required property string modelData
                    Layout.fillWidth: true
                    orientation: Qt.Horizontal
                    transparent: !(root.currentSection === modelData && !root.searching)
                    accentButton: root.currentSection === modelData && !root.searching
                    text: modelData
                    navigation.panel: navPanel
                    onClicked: {
                        searchField.clear()
                        root.searchText = ""
                        root.currentSection = modelData
                        root.openTopic = null
                    }
                }
            }

            Item { Layout.fillHeight: true }

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.Wrap
                opacity: 0.6
                text: qsTrc("appshell", "Everything here works offline and describes this version of Jubelle.")
            }
        }

        Rectangle {
            Layout.fillHeight: true
            width: 1
            color: ui.theme.strokeColor
        }

        // ---------------- content ----------------
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // a list of guides
            ColumnLayout {
                anchors.fill: parent
                spacing: 12
                visible: root.openTopic === null
                         && (root.searching || (root.currentSection !== root.rangesSection && root.currentSection !== root.shortcutsSection))

                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    font: ui.theme.largeBodyBoldFont
                    text: root.searching
                          ? qsTrc("appshell", "%n guide(s) mention “%1”", "", root.shownTopics.length).arg(root.searchText.trim())
                          : root.currentSection
                }

                StyledListView {
                    id: topicList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 8
                    model: root.shownTopics

                    delegate: ListItemBlank {
                        id: topicRow
                        required property var modelData
                        width: topicList.width
                        height: topicText.implicitHeight + 24
                        radius: 8
                        normalColor: ui.theme.backgroundPrimaryColor

                        onClicked: root.openTopic = topicRow.modelData

                        ColumnLayout {
                            id: topicText
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            spacing: 2

                            StyledTextLabel {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignLeft
                                font: ui.theme.bodyBoldFont
                                text: topicRow.modelData.title
                            }
                            StyledTextLabel {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignLeft
                                wrapMode: Text.Wrap
                                opacity: 0.75
                                text: (root.searching ? topicRow.modelData.section + " · " : "") + topicRow.modelData.summary
                            }
                        }
                    }
                }
            }

            // one guide
            ColumnLayout {
                anchors.fill: parent
                spacing: 12
                visible: root.openTopic !== null

                FlatButton {
                    orientation: Qt.Horizontal
                    icon: IconCode.ARROW_LEFT
                    text: qsTrc("appshell", "All guides")
                    onClicked: root.openTopic = null
                }

                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.Wrap
                    font: ui.theme.titleBoldFont
                    text: root.openTopic ? root.openTopic.title : ""
                }

                StyledFlickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentHeight: guideColumn.implicitHeight

                    ColumnLayout {
                        id: guideColumn
                        width: Math.min(parent.width, 760)
                        spacing: 12

                        StyledTextLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            wrapMode: Text.Wrap
                            opacity: 0.75
                            font: ui.theme.largeBodyFont
                            text: root.openTopic ? root.openTopic.summary : ""
                        }

                        Repeater {
                            model: root.openTopic ? root.openTopic.body : []

                            StyledTextLabel {
                                required property string modelData
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignLeft
                                wrapMode: Text.Wrap
                                textFormat: Text.StyledText
                                lineHeight: 1.25
                                text: modelData
                            }
                        }
                    }
                }
            }

            // instrument ranges and transposition
            ColumnLayout {
                anchors.fill: parent
                spacing: 12
                visible: !root.searching && root.currentSection === root.rangesSection

                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    font: ui.theme.largeBodyBoldFont
                    text: root.rangesSection
                }
                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.Wrap
                    opacity: 0.75
                    text: qsTrc("appshell", "Ranges are shown as written for the player (middle C is C4). “Comfortable” is the safe range; “Full” is what a professional can play. These are the same limits Jubelle uses when it colours out-of-range notes.")
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    SearchField {
                        id: instrumentSearch
                        Layout.preferredWidth: 280
                        hint: qsTrc("appshell", "Find an instrument or family")
                        onSearchTextChanged: lc.setSearch(instrumentSearch.searchText)
                    }
                    CheckBox {
                        text: qsTrc("appshell", "Transposing instruments only")
                        checked: lc.onlyTransposing
                        onClicked: lc.setOnlyTransposing(!checked)
                    }
                    Item { Layout.fillWidth: true }
                }

                // Jubelle: which instruments: all, an ensemble (from the templates), or your own selection
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    StyledTextLabel {
                        text: qsTrc("appshell", "Show")
                    }
                    StyledDropdown {
                        id: presetDropdown
                        Layout.preferredWidth: 280
                        model: {
                            var items = [ { text: qsTrc("appshell", "All instruments"), value: -1 },
                                          { text: qsTrc("appshell", "My selection (★)"), value: -2 } ]
                            var presets = lc.presets
                            for (var i = 0; i < presets.length; ++i) {
                                items.push({ text: presets[i].name, value: i })
                            }
                            return items
                        }
                        currentIndex: indexOfValue(lc.preset)
                        onActivated: function(index, value) { lc.preset = value }
                    }
                    FlatButton {
                        text: qsTrc("appshell", "Fold all")
                        onClicked: lc.setAllFamiliesCollapsed(true)
                    }
                    FlatButton {
                        text: qsTrc("appshell", "Unfold all")
                        onClicked: lc.setAllFamiliesCollapsed(false)
                    }
                    Item { Layout.fillWidth: true }
                    StyledTextLabel {
                        opacity: 0.6
                        text: qsTrc("appshell", "%n instrument(s)", "", lc.instrumentCount)
                    }
                }

                StyledListView {
                    id: instrumentList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: lc.instruments

                    delegate: Rectangle {
                        id: instrumentRow
                        required property var modelData
                        width: instrumentList.width
                        height: modelData.isHeader ? familyHeader.implicitHeight + 12 : instrumentText.implicitHeight + 16
                        radius: 6
                        color: modelData.isHeader ? "transparent" : ui.theme.backgroundPrimaryColor

                        // Jubelle: a family heading; click to fold or unfold it
                        FlatButton {
                            id: familyHeader
                            visible: instrumentRow.modelData.isHeader
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            transparent: true
                            orientation: Qt.Horizontal
                            icon: instrumentRow.modelData.collapsed ? IconCode.SMALL_ARROW_RIGHT : IconCode.SMALL_ARROW_DOWN
                            text: visible ? qsTrc("appshell", "%1 (%2)").arg(instrumentRow.modelData.title).arg(instrumentRow.modelData.count) : ""
                            textFont: ui.theme.bodyBoldFont
                            onClicked: lc.toggleFamily(instrumentRow.modelData.family)
                        }

                        RowLayout {
                            id: instrumentText
                            visible: !instrumentRow.modelData.isHeader
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 12

                            FlatButton {
                                transparent: true
                                text: instrumentRow.modelData.selected ? "★" : "☆"
                                toolTipTitle: instrumentRow.modelData.selected ? qsTrc("appshell", "Remove from my selection")
                                                                               : qsTrc("appshell", "Add to my selection")
                                onClicked: lc.toggleSelected(instrumentRow.modelData.id)
                            }
                            ColumnLayout {
                                Layout.preferredWidth: 230
                                Layout.maximumWidth: 230
                                spacing: 0
                                StyledTextLabel {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignLeft
                                    font: ui.theme.bodyBoldFont
                                    text: instrumentRow.modelData.name || ""   // header rows have no name
                                }
                                StyledTextLabel {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignLeft
                                    opacity: 0.6
                                    text: instrumentRow.modelData.family
                                }
                            }
                            ColumnLayout {
                                Layout.preferredWidth: 220
                                Layout.maximumWidth: 220
                                spacing: 0
                                StyledTextLabel {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignLeft
                                    text: qsTrc("appshell", "Comfortable: %1").arg(instrumentRow.modelData.comfortable)
                                }
                                StyledTextLabel {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignLeft
                                    opacity: 0.75
                                    text: qsTrc("appshell", "Full: %1").arg(instrumentRow.modelData.written)
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                StyledTextLabel {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignLeft
                                    wrapMode: Text.Wrap
                                    color: instrumentRow.modelData.transposing ? root.jubellePurple : ui.theme.fontPrimaryColor
                                    text: instrumentRow.modelData.transposition || ""
                                }
                                StyledTextLabel {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignLeft
                                    opacity: 0.75
                                    visible: instrumentRow.modelData.transposing
                                    text: qsTrc("appshell", "Sounding range: %1").arg(instrumentRow.modelData.sounding)
                                }
                            }
                        }
                    }
                }
            }

            // keyboard shortcuts
            ColumnLayout {
                anchors.fill: parent
                spacing: 12
                visible: !root.searching && root.currentSection === root.shortcutsSection

                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    font: ui.theme.largeBodyBoldFont
                    text: root.shortcutsSection
                }
                StyledTextLabel {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 760
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.Wrap
                    textFormat: Text.StyledText
                    lineHeight: 1.25
                    text: qsTrc("appshell", "Every command in Jubelle can have a keyboard shortcut, including Jubelle's own (Find and Select, Linked Passages, Record tempo, Page turns, Proofread score and the rest). Open <b>Preferences › Shortcuts</b>, type part of a command's name in the search box, and press the keys you want. The list there always shows your current shortcuts, and you can print it or save it to a file.")
                }
                StyledTextLabel {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 760
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.Wrap
                    textFormat: Text.StyledText
                    lineHeight: 1.25
                    text: qsTrc("appshell", "Jubelle keeps MuseScore's shortcuts, so what you know still works: <b>N</b> note input, <b>1–9</b> durations, <b>A–G</b> pitches, <b>0</b> rest, <b>Ctrl+↑/↓</b> octave, <b>S</b> slur, <b>T</b> tie, <b>Space</b> play, <b>Ctrl+Z</b> undo. In Record tempo, any key is a tap.")
                }
                FlatButton {
                    accentButton: true
                    text: qsTrc("appshell", "Open Preferences")
                    onClicked: lc.openShortcuts()
                }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
