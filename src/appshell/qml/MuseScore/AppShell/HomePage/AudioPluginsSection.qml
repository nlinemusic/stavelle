/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle: VST3 instruments and effects on the Plugins page: each plugin with
// its maker and kind, an on/off switch (kept across rescans), a plain reason
// for any plugin that could not be loaded, and Rescan.

import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.AppShell

Item {
    id: root

    property string search: ""
    property NavigationSection navigationSection: null
    property real sideMargin: 46

    AudioPluginsModel {
        id: audioModel
    }

    Component.onCompleted: audioModel.load()
    onSearchChanged: audioModel.setSearch(root.search)

    NavigationPanel {
        id: navPanel
        name: "AudioPluginsList"
        section: root.navigationSection
        order: 3
        direction: NavigationPanel.Both
        accessible.name: qsTrc("appshell", "Audio plugins")
    }

    Column {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.leftMargin: root.sideMargin
        anchors.right: parent.right
        anchors.rightMargin: root.sideMargin
        spacing: 8

        RowLayout {
            width: parent.width
            spacing: 12

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                text: audioModel.failedCount > 0
                      ? qsTrc("appshell", "%n plugin(s) could not be loaded. The reason is shown under each one.", "", audioModel.failedCount)
                      : qsTrc("appshell", "VST3 instruments and effects found on this computer. Switch one off to hide it from the instrument and effect menus.")
            }

            FlatButton {
                text: qsTrc("appshell", "Rescan")
                icon: IconCode.UPDATE
                orientation: Qt.Horizontal
                toolTipTitle: qsTrc("appshell", "Look for new, changed or removed plugins")
                navigation.name: "AudioPluginsRescan"
                navigation.panel: navPanel
                navigation.row: 0
                navigation.column: 0
                onClicked: audioModel.rescan()
            }
        }
    }

    StyledListView {
        id: list
        anchors.top: header.bottom
        anchors.topMargin: 16
        anchors.left: parent.left
        anchors.leftMargin: root.sideMargin
        anchors.right: parent.right
        anchors.rightMargin: root.sideMargin
        anchors.bottom: parent.bottom
        spacing: 8
        clip: true

        model: audioModel.plugins

        delegate: Rectangle {
            required property var modelData
            required property int index

            width: ListView.view.width
            height: row.implicitHeight + 20
            radius: 4
            color: ui.theme.backgroundPrimaryColor
            border.color: modelData.failed ? "#E5484D" : ui.theme.strokeColor
            border.width: 1

            RowLayout {
                id: row
                anchors.fill: parent
                anchors.margins: 10
                spacing: 12

                Column {
                    Layout.fillWidth: true
                    spacing: 2

                    StyledTextLabel {
                        width: parent.width
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideRight
                        font: ui.theme.bodyBoldFont
                        text: modelData.id
                    }

                    StyledTextLabel {
                        width: parent.width
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideRight
                        opacity: 0.7
                        text: [modelData.kind, modelData.vendor].filter(function(s) { return s !== "" }).join(" · ")
                    }

                    StyledTextLabel {
                        width: parent.width
                        visible: modelData.failed
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.WordWrap
                        color: "#E5484D"
                        text: modelData.error
                    }

                    StyledTextLabel {
                        width: parent.width
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideMiddle
                        opacity: 0.5
                        text: modelData.path
                    }
                }

                ToggleButton {
                    visible: modelData.canToggle
                    checked: modelData.enabled
                    text: modelData.enabled ? qsTrc("appshell", "On") : qsTrc("appshell", "Off")
                    navigation.panel: navPanel
                    navigation.row: index + 1
                    navigation.column: 0
                    navigation.accessible.name: modelData.id
                    onToggled: audioModel.setPluginEnabled(modelData.id, !checked)
                }
            }
        }

        StyledTextLabel {
            anchors.centerIn: parent
            visible: list.count === 0
            text: qsTrc("appshell", "No VST3 plugins found. Install them in the standard VST3 folder, then press Rescan.")
        }
    }
}
