/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.NotationScene

//! Jubelle G5: Tools › Proofread score, docked like MuseScore's other panels.
//! Findings grouped by check; click one to go there. Nothing in the score is
//! ever changed by proofreading.
Item {
    id: root

    property NavigationSection navigationSection: null
    property int navigationOrderStart: 0

    readonly property var severityColors: ["#EF4444", "#F59E0B", "#A855F7"]

    ProofreadModel {
        id: pm
    }

    Component.onCompleted: pm.load()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            FlatButton {
                text: qsTrc("notation/proofread", "Check again")
                accentButton: true
                enabled: pm.hasScore
                onClicked: pm.check()
            }
            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                text: qsTrc("notation/proofread", "%1 errors · %2 warnings · %3 suggestions")
                      .arg(pm.errorCount).arg(pm.warningCount).arg(pm.suggestionCount)
            }
        }

        // which checks run
        Flow {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: pm.categories
                CheckBox {
                    required property var modelData
                    text: modelData.title
                    checked: modelData.on
                    onClicked: pm.setCategory(modelData.category, !checked)
                }
            }
        }
        CheckBox {
            text: qsTrc("notation/proofread", "Also show findings marked as intentional")
            checked: pm.showIntentional
            onClicked: pm.setShowIntentional(!checked)
        }

        SeparatorLine { Layout.fillWidth: true }

        StyledTextLabel {
            Layout.fillWidth: true
            visible: pm.findings.length === 0
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            opacity: 0.75
            text: pm.hasScore ? qsTrc("notation/proofread", "Nothing to report for the checks that are on.")
                              : qsTrc("notation/proofread", "Open a score to proofread it.")
        }

        StyledListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: pm.findings

            delegate: ListItemBlank {
                id: row
                required property var modelData
                width: list.width
                height: content.implicitHeight + 12
                radius: 6
                normalColor: ui.theme.backgroundSecondaryColor

                onClicked: pm.goTo(row.modelData.index)

                RowLayout {
                    id: content
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 8
                    anchors.rightMargin: 4
                    spacing: 8

                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 4
                        width: 8
                        height: 8
                        radius: 4
                        color: root.severityColors[row.modelData.severity]
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        StyledTextLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            wrapMode: Text.Wrap
                            text: row.modelData.message
                            opacity: row.modelData.intentional ? 0.6 : 1.0
                        }
                        StyledTextLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            opacity: 0.6
                            text: {
                                var t = row.modelData.categoryTitle
                                if (row.modelData.bar > 0) {
                                    t += " · " + qsTrc("notation/proofread", "bar %1").arg(row.modelData.bar)
                                }
                                if (row.modelData.intentional) {
                                    t += " · " + qsTrc("notation/proofread", "marked as intentional")
                                }
                                return t
                            }
                        }
                    }
                    FlatButton {
                        icon: IconCode.CLOSE_X_ROUNDED
                        transparent: true
                        toolTipTitle: qsTrc("notation/proofread", "Dismiss")
                        toolTipDescription: qsTrc("notation/proofread", "Hide this until you check again")
                        onClicked: pm.dismiss(row.modelData.index)
                    }
                    FlatButton {
                        icon: IconCode.TICK_RIGHT_ANGLE
                        transparent: true
                        toolTipTitle: row.modelData.intentional ? qsTrc("notation/proofread", "Not intentional")
                                                                : qsTrc("notation/proofread", "Mark as intentional")
                        toolTipDescription: qsTrc("notation/proofread", "Remembered in the score, so it isn't reported again")
                        onClicked: pm.setIntentional(row.modelData.index, !row.modelData.intentional)
                    }
                }
            }
        }
    }
}
