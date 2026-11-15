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

//! Jubelle: Format → Apply Part Layout…  Copies one part's layout (never its
//! music) to the parts you tick, when you press Apply Layout. One undo step.
StyledDialogView {
    id: root

    title: qsTrc("notation", "Apply Part Layout")
    objectName: "ApplyPartLayoutDialog"

    contentWidth: 600
    contentHeight: 640
    margins: 24

    modal: true

    readonly property color jubelleMagenta: "#E600A9"
    readonly property color jubellePurple: "#A855F7"

    ApplyPartLayoutModel {
        id: layoutModel
    }

    Component.onCompleted: layoutModel.load()

    NavigationPanel {
        id: panel
        name: "ApplyPartLayoutPanel"
        section: root.navigationSection
        direction: NavigationPanel.Vertical
        order: 1
    }

    component SectionTitle: StyledTextLabel {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignLeft
        font: ui.theme.bodyBoldFont
        color: root.jubellePurple
    }

    component Option: CheckBox {
        Layout.fillWidth: true
        navigation.panel: panel
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        StyledTextLabel {
            Layout.fillWidth: true
            visible: layoutModel.sources.length === 0
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            text: qsTrc("notation", "This score has no parts yet. Create parts first (File → Parts…).")
        }

        SectionTitle { text: qsTrc("notation", "Copy the layout of") }
        StyledDropdown {
            Layout.fillWidth: true
            enabled: !layoutModel.sourceFixed
            model: layoutModel.sources
            currentIndex: layoutModel.sourceIndex
            navigation.panel: panel
            onActivated: function(index, value) { layoutModel.sourceIndex = index }
        }

        RowLayout {
            Layout.fillWidth: true
            SectionTitle { text: qsTrc("notation", "To these parts") }
            FlatButton {
                text: qsTrc("notation", "Select all")
                navigation.panel: panel
                onClicked: layoutModel.selectAll()
            }
            FlatButton {
                text: qsTrc("notation", "Clear selection")
                navigation.panel: panel
                onClicked: layoutModel.clearSelection()
            }
        }

        StyledListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
            clip: true
            model: layoutModel.destinations

            delegate: Column {
                width: ListView.view.width
                spacing: 2

                RowLayout {
                    width: parent.width
                    CheckBox {
                        text: modelData.name
                        checked: modelData.checked
                        navigation.panel: panel
                        onClicked: layoutModel.setDestinationChecked(modelData.row, !checked)
                    }
                    Item { Layout.fillWidth: true }
                    StyledTextLabel {
                        text: modelData.status
                        color: modelData.issues !== "" ? root.jubelleMagenta : ui.theme.fontPrimaryColor
                    }
                }
                StyledTextLabel {
                    visible: modelData.issues !== ""
                    width: parent.width
                    leftPadding: 28
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.Wrap
                    opacity: 0.8
                    text: modelData.issues
                }
            }
        }

        SectionTitle { text: qsTrc("notation", "What to copy") }
        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            opacity: 0.7
            text: qsTrc("notation", "System breaks, page breaks and keep-together marks are always copied. The parts’ own items of each kind you copy are replaced. Notes and other music are never copied or changed.")
        }
        Option {
            text: qsTrc("notation", "System locks")
            checked: layoutModel.systemLocks
            onClicked: layoutModel.systemLocks = !checked
        }
        Option {
            text: qsTrc("notation", "Measure widths")
            checked: layoutModel.measureWidths
            onClicked: layoutModel.measureWidths = !checked
        }
        Option {
            text: qsTrc("notation", "Staff spacers (skipped for parts with a different number of staves)")
            checked: layoutModel.spacers
            onClicked: layoutModel.spacers = !checked
        }
        Option {
            text: qsTrc("notation", "Page and spacing settings (page size, margins, staff size, distances, multi-measure rests)")
            checked: layoutModel.pageAndSpacing
            onClicked: layoutModel.pageAndSpacing = !checked
        }
        Option {
            text: qsTrc("notation", "Also copy breaks that fall inside a part’s multi-measure rests (they have no effect there until rests are split)")
            checked: layoutModel.breaksInsideMMRests
            onClicked: layoutModel.breaksInsideMMRests = !checked
        }

        StyledTextLabel {
            Layout.fillWidth: true
            visible: text !== ""
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            color: root.jubelleMagenta
            text: layoutModel.resultText
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            NavigationPanel {
                id: buttonsPanel
                name: "ApplyPartLayoutButtons"
                section: root.navigationSection
                direction: NavigationPanel.Horizontal
                order: 2
            }

            Item { Layout.fillWidth: true }

            FlatButton {
                text: layoutModel.resultText !== "" ? qsTrc("global", "Close") : qsTrc("global", "Cancel")
                navigation.panel: buttonsPanel
                navigation.column: 1
                onClicked: root.hide()
            }
            FlatButton {
                text: qsTrc("notation", "Apply Layout")
                accentButton: true
                enabled: layoutModel.canApply
                navigation.panel: buttonsPanel
                navigation.column: 2
                onClicked: layoutModel.apply()
            }
        }
    }
}
