/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
import QtQuick
import QtQuick.Layouts

import MuseScore.NotationScene
import Muse.Ui
import Muse.UiComponents

//! Jubelle G7: Format › Style › Jubelle layout. Magnetic layout and collision
//! avoidance work on top of MuseScore's own placement; with both off, the
//! score lays out exactly as in MuseScore. Items you dragged, or with
//! Automatic placement unticked, are never moved.
StyledFlickable {
    id: root

    contentWidth: Math.max(contentLayout.implicitWidth, root.width)
    contentHeight: contentLayout.implicitHeight

    JubelleLayoutPageModel {
        id: pageModel
    }

    readonly property bool magneticOn: pageModel.magneticLayout.value === true

    component Note: StyledTextLabel {
        Layout.fillWidth: true
        Layout.leftMargin: 28
        horizontalAlignment: Text.AlignLeft
        wrapMode: Text.Wrap
        opacity: 0.75
    }

    ColumnLayout {
        id: contentLayout
        width: parent.width
        spacing: 12

        StyledGroupBox {
            Layout.fillWidth: true
            title: qsTrc("notation/editstyle/jubellelayout", "Magnetic layout")

            ColumnLayout {
                width: parent.width
                spacing: 8

                CheckBox {
                    text: qsTrc("notation/editstyle/jubellelayout", "Use magnetic layout")
                    checked: root.magneticOn
                    onClicked: pageModel.magneticLayout.value = !root.magneticOn
                }
                Note { text: qsTrc("notation/editstyle/jubellelayout", "Markings line up and stay close to their notes. MuseScore’s own placement is kept underneath; this only refines it.") }

                CheckBox {
                    Layout.leftMargin: 28
                    enabled: root.magneticOn
                    text: qsTrc("notation/editstyle/jubellelayout", "Line up in rows")
                    checked: pageModel.magneticRows.value === true
                    onClicked: pageModel.magneticRows.value = !(pageModel.magneticRows.value === true)
                }
                Note {
                    Layout.leftMargin: 56
                    text: qsTrc("notation/editstyle/jubellelayout", "Dynamics, expressions and hairpins on a staff share a row, and so do rehearsal marks and tempo marks. A marking that has to make room for a high or low note leaves the row only there.")
                }

                CheckBox {
                    Layout.leftMargin: 28
                    enabled: root.magneticOn
                    text: qsTrc("notation/editstyle/jubellelayout", "Nudge sideways instead of pushing away")
                    checked: pageModel.sidewaysNudge.value === true
                    onClicked: pageModel.sidewaysNudge.value = !(pageModel.sidewaysNudge.value === true)
                }
                Note {
                    Layout.leftMargin: 56
                    text: qsTrc("notation/editstyle/jubellelayout", "Dynamics, expressions and staff text may move a little left or right when that keeps them much closer to the staff. Chord symbols stay over their beat.")
                }

                StyleSpinboxWithReset {
                    Layout.leftMargin: 56
                    enabled: root.magneticOn && pageModel.sidewaysNudge.value === true
                    styleItem: pageModel.nudgeLimit
                    label: qsTrc("notation/editstyle/jubellelayout", "At most:")
                    suffix: qsTrc("global", "sp")
                    decimals: 2
                    step: 0.25
                    min: 0.0
                    max: 2.0
                    labelAreaWidth: -1
                    controlAreaWidth: spinBoxWidth
                }
            }
        }

        StyledGroupBox {
            Layout.fillWidth: true
            title: qsTrc("notation/editstyle/jubellelayout", "Collision avoidance")

            ColumnLayout {
                width: parent.width
                spacing: 8

                CheckBox {
                    text: qsTrc("notation/editstyle/jubellelayout", "Move markings out of collisions")
                    checked: pageModel.collisionAvoidance.value === true
                    onClicked: pageModel.collisionAvoidance.value = !(pageModel.collisionAvoidance.value === true)
                }
                Note { text: qsTrc("notation/editstyle/jubellelayout", "Where two markings still overlap after placement, the less important one moves: sideways first if nudging is on, otherwise away from the staff. Order of importance: notes, articulations, slurs and ties, dynamics and hairpins, lyrics, text, rehearsal and tempo marks.") }
            }
        }

        StyledGroupBox {
            Layout.fillWidth: true
            title: qsTrc("notation/editstyle/jubellelayout", "Engraving rules")

            ColumnLayout {
                width: parent.width
                spacing: 8

                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.Wrap
                    opacity: 0.75
                    text: qsTrc("notation/editstyle/jubellelayout", "Say where a kind of marking goes, and how far from the staff it stays at least, for all instruments, a group or one instrument. The most specific rule wins. Anything you set by hand in Properties still wins over a rule.")
                }

                RowLayout {
                    spacing: 8
                    StyledTextLabel { text: qsTrc("notation/editstyle/jubellelayout", "Start from:") }
                    StyledDropdown {
                        id: presetDropdown
                        Layout.preferredWidth: 220
                        model: pageModel.presetOptions
                        textRole: "text"
                        valueRole: "value"
                        currentIndex: -1
                        displayText: qsTrc("notation/editstyle/jubellelayout", "Choose a preset…")
                        onActivated: function(index, value) { pageModel.applyPreset(value) }
                    }
                }

                Repeater {
                    model: pageModel.rules

                    delegate: RowLayout {
                        id: ruleRow
                        required property var modelData
                        required property int index
                        spacing: 6

                        StyledDropdown {
                            Layout.preferredWidth: 130
                            model: pageModel.kindOptions
                            textRole: "text"
                            valueRole: "value"
                            currentIndex: indexOfValue(ruleRow.modelData.kind)
                            onActivated: function(i, value) { pageModel.setRuleField(ruleRow.index, "kind", value) }
                        }
                        StyledDropdown {
                            Layout.preferredWidth: 180
                            model: pageModel.scopeOptions
                            textRole: "text"
                            valueRole: "value"
                            currentIndex: indexOfValue(ruleRow.modelData.scope)
                            onActivated: function(i, value) { pageModel.setRuleField(ruleRow.index, "scope", value) }
                        }
                        StyledDropdown {
                            Layout.preferredWidth: 150
                            model: pageModel.placementOptions
                            textRole: "text"
                            valueRole: "value"
                            currentIndex: indexOfValue(ruleRow.modelData.placement)
                            onActivated: function(i, value) { pageModel.setRuleField(ruleRow.index, "placement", value) }
                        }
                        CheckBox {
                            text: qsTrc("notation/editstyle/jubellelayout", "At least")
                            checked: ruleRow.modelData.hasDistance
                            onClicked: pageModel.setRuleField(ruleRow.index, "distance", ruleRow.modelData.hasDistance ? "" : 1.0)
                        }
                        IncrementalPropertyControl {
                            Layout.preferredWidth: 80
                            enabled: ruleRow.modelData.hasDistance
                            currentValue: ruleRow.modelData.distance
                            measureUnitsSymbol: qsTrc("global", "sp")
                            step: 0.25
                            decimals: 2
                            minValue: -10
                            maxValue: 20
                            onValueEditingFinished: function(newValue) { pageModel.setRuleField(ruleRow.index, "distance", newValue) }
                        }
                        FlatButton {
                            icon: IconCode.DELETE_TANK
                            toolTipTitle: qsTrc("notation/editstyle/jubellelayout", "Remove this rule")
                            onClicked: pageModel.removeRule(ruleRow.index)
                        }
                    }
                }

                FlatButton {
                    text: qsTrc("notation/editstyle/jubellelayout", "Add rule")
                    onClicked: pageModel.addRule()
                }
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            opacity: 0.75
            text: qsTrc("notation/editstyle/jubellelayout", "Markings you have dragged, or with Automatic placement unticked in Properties, are never moved. New Jubelle scores start with both on; scores made earlier keep MuseScore’s layout until you switch them on.")
        }
    }
}
