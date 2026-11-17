/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.NotationScene

//! Jubelle G8: Format › Page turns…  Where the page turns fall, and better places for them.
StyledDialogView {
    id: root

    title: qsTrc("notation/pageturns", "Page turns")
    objectName: "PageTurnsDialog"

    contentWidth: 620
    contentHeight: 620
    margins: 24

    readonly property var qualityColors: ["#2EB67D", "#E0A100", "#E5484D"]

    PageTurnsModel {
        id: pt
    }

    Component.onCompleted: pt.load()

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            text: qsTrc("notation/pageturns", "Jubelle looks at the rest around every page turn and can move a turn back to the nearest place with enough rest by adding a page break. No music is changed, and page breaks you put in yourself are kept.")
        }

        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 8

            StyledTextLabel { text: qsTrc("notation/pageturns", "Part:") }
            StyledDropdown {
                Layout.preferredWidth: 360
                model: pt.parts
                currentIndex: pt.partIndex
                onActivated: function(i, v) { pt.setPartIndex(v) }
            }

            StyledTextLabel { text: qsTrc("notation/pageturns", "Pages:") }
            StyledDropdown {
                Layout.preferredWidth: 360
                model: pt.layoutOptions
                currentIndex: pt.layoutIndex
                onActivated: function(i, v) { pt.setLayoutIndex(v) }
            }

            StyledTextLabel { text: qsTrc("notation/pageturns", "Rest needed to turn:") }
            IncrementalPropertyControl {
                Layout.preferredWidth: 110
                currentValue: pt.neededSeconds
                measureUnitsSymbol: qsTrc("global", "s")
                step: 0.5
                decimals: 1
                minValue: 0.5
                maxValue: 20
                onValueEditingFinished: function(v) { pt.setNeededSeconds(v) }
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            font: ui.theme.bodyBoldFont
            visible: text !== ""
            text: pt.summary
        }

        StyledListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: pt.turns

            delegate: ListItemBlank {
                id: row
                required property var modelData
                width: list.width
                height: content.implicitHeight + 12
                radius: 6
                normalColor: ui.theme.backgroundSecondaryColor

                onClicked: pt.goTo(row.modelData.bar)

                RowLayout {
                    id: content
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8

                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        width: 10
                        height: 10
                        radius: 5
                        color: root.qualityColors[row.modelData.quality]
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        StyledTextLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            text: row.modelData.title
                        }
                        StyledTextLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            opacity: 0.75
                            text: row.modelData.detail
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            FlatButton {
                text: qsTrc("notation/pageturns", "Remove Jubelle's turns")
                enabled: pt.hasJubelleTurns
                toolTipTitle: text
                toolTipDescription: qsTrc("notation/pageturns", "Removes the page breaks Jubelle added, in every part. Your own page breaks stay.")
                onClicked: pt.clear(true)
            }
            Item { Layout.fillWidth: true }
            FlatButton {
                text: qsTrc("global", "Close")
                onClicked: root.hide()
            }
            FlatButton {
                text: qsTrc("notation/pageturns", "Preview")
                onClicked: pt.preview()
            }
            FlatButton {
                text: qsTrc("notation/pageturns", "Apply to all parts")
                enabled: pt.parts.length > 1
                onClicked: pt.apply(true)
            }
            FlatButton {
                accentButton: true
                text: qsTrc("notation/pageturns", "Apply to this part")
                onClicked: pt.apply(false)
            }
        }
    }
}
