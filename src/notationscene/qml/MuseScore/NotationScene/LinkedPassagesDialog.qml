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

//! Jubelle: Tools → Linked Passages…
//! Top: link the highlighted passage to the same bars on other staves.
//! Below: every link in the score, with what it follows, and for each staff
//! Show / Re-sync / Unlink. Every change is applied at once as one undo step.
StyledDialogView {
    id: root

    title: qsTrc("notation", "Linked Passages")
    objectName: "LinkedPassagesDialog"

    contentWidth: 560
    contentHeight: 620
    margins: 20

    readonly property color jubelleMagenta: "#E600A9"
    readonly property color jubellePurple: "#A855F7"

    LinkedPassagesModel {
        id: linkModel
    }

    Component.onCompleted: linkModel.load()

    NavigationPanel {
        id: panel
        name: "LinkedPassagesPanel"
        section: root.navigationSection
        direction: NavigationPanel.Vertical
        order: 1
    }

    //! What a link follows; also used for each existing link
    component SyncChecks: Flow {
        id: checks
        // Inline components can't see the dialog's ids, so the panel is passed in
        property NavigationPanel navPanel: null
        property var sync: ({ rhythm: true, durations: true, articulations: true, dynamics: true, markings: true })
        signal toggled(string option, bool on)
        spacing: 12
        Layout.fillWidth: true

        Repeater {
            model: [
                { key: "rhythm", text: qsTrc("notation", "Rhythm") },
                { key: "durations", text: qsTrc("notation", "Note lengths") },
                { key: "articulations", text: qsTrc("notation", "Articulations") },
                { key: "dynamics", text: qsTrc("notation", "Dynamics") },
                { key: "markings", text: qsTrc("notation", "Slurs and ties") }
            ]
            delegate: CheckBox {
                text: modelData.text
                checked: checks.sync[modelData.key]
                navigation.panel: checks.navPanel
                onClicked: checks.toggled(modelData.key, !checked)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        StyledTextLabel {
            Layout.fillWidth: true
            visible: linkModel.problem !== ""
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            color: root.jubelleMagenta
            text: linkModel.problem
        }

        // ---- Link the highlighted passage ----
        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            font: ui.theme.bodyBoldFont
            color: root.jubellePurple
            text: linkModel.selectionTitle !== ""
                  ? qsTrc("notation", "Link %1 to:").arg(linkModel.selectionTitle)
                  : qsTrc("notation", "To link a passage, highlight its bars on the staff that leads, then open this again.")
            wrapMode: Text.Wrap
        }

        StyledFlickable {
            visible: linkModel.selectionTitle !== ""
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(140, candidates.implicitHeight)
            contentHeight: candidates.implicitHeight

            Column {
                id: candidates
                width: parent.width
                spacing: 4

                Repeater {
                    model: linkModel.candidateStaves
                    delegate: CheckBox {
                        text: modelData.name
                        checked: modelData.checked
                        navigation.panel: panel
                        onClicked: linkModel.setCandidateChecked(modelData.staff, !checked)
                    }
                }
            }
        }

        StyledTextLabel {
            visible: linkModel.selectionTitle !== ""
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            text: qsTrc("notation", "The linked staves follow:")
        }

        SyncChecks {
            id: newSync
            navPanel: panel
            visible: linkModel.selectionTitle !== ""
            onToggled: function(option, on) {
                var s = Object.assign({}, newSync.sync)
                s[option] = on
                newSync.sync = s
            }
        }

        StyledTextLabel {
            visible: linkModel.selectionTitle !== ""
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            opacity: 0.7
            text: qsTrc("notation", "Each staff keeps its own pitches. Nothing changes now; from here on, edits to the leading staff are copied. Notes you change by hand on a linked staff stay yours until you re-sync it.")
        }

        FlatButton {
            visible: linkModel.selectionTitle !== ""
            text: qsTrc("notation", "Link")
            accentButton: true
            navigation.panel: panel
            onClicked: linkModel.link(newSync.sync.rhythm, newSync.sync.durations, newSync.sync.articulations,
                                      newSync.sync.dynamics, newSync.sync.markings)
        }

        SeparatorLine { Layout.fillWidth: true }

        // ---- Links in this score ----
        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            font: ui.theme.bodyBoldFont
            text: linkModel.groups.length > 0 ? qsTrc("notation", "Links in this score")
                                              : qsTrc("notation", "No linked passages in this score.")
        }

        StyledFlickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: groupList.implicitHeight

            Column {
                id: groupList
                width: parent.width
                spacing: 14

                Repeater {
                    model: linkModel.groups

                    delegate: Column {
                        id: groupItem
                        property var group: modelData
                        width: groupList.width
                        spacing: 6

                        StyledTextLabel {
                            width: parent.width
                            horizontalAlignment: Text.AlignLeft
                            color: root.jubellePurple
                            text: groupItem.group.title
                        }

                        SyncChecks {
                            width: parent.width
                            navPanel: panel
                            sync: groupItem.group.sync
                            onToggled: function(option, on) { linkModel.setSync(groupItem.group.id, option, on) }
                        }

                        Repeater {
                            model: groupItem.group.members

                            delegate: RowLayout {
                                width: groupList.width
                                spacing: 8

                                StyledTextLabel {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignLeft
                                    elide: Text.ElideRight
                                    text: modelData.name + " · " + modelData.bars
                                          + (modelData.isSource ? "  " + qsTrc("notation", "(leads)") : "")
                                }

                                StyledTextLabel {
                                    visible: !modelData.isSource && modelData.ownNotes > 0
                                    color: root.jubelleMagenta
                                    text: qsTrc("notation", "%n note(s) your own", "", modelData.ownNotes)
                                }

                                FlatButton {
                                    text: qsTrc("notation", "Show")
                                    navigation.panel: panel
                                    onClicked: linkModel.show(groupItem.group.id, modelData.index)
                                }

                                FlatButton {
                                    visible: !modelData.isSource
                                    text: qsTrc("notation", "Re-sync")
                                    toolTipTitle: qsTrc("notation", "Make this staff match the leading staff again, including notes you changed by hand (pitches are kept)")
                                    navigation.panel: panel
                                    onClicked: linkModel.resync(groupItem.group.id, modelData.index)
                                }

                                FlatButton {
                                    text: qsTrc("notation", "Unlink")
                                    toolTipTitle: modelData.isSource
                                                  ? qsTrc("notation", "Stop this staff leading; the next linked staff leads instead")
                                                  : qsTrc("notation", "Stop following; the music stays as it is")
                                    navigation.panel: panel
                                    onClicked: linkModel.unlink(groupItem.group.id, modelData.index)
                                }
                            }
                        }
                    }
                }
            }
        }

        // Why a linked staff couldn't be updated by the last edit
        Repeater {
            model: linkModel.messages
            delegate: StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.Wrap
                color: root.jubelleMagenta
                text: modelData
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            FlatButton {
                text: qsTrc("global", "Close")
                accentButton: true
                navigation.panel: panel
                onClicked: root.hide()
            }
        }
    }
}
