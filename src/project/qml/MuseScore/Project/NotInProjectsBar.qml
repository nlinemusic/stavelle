/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

//! Jubelle R1: shown under the tabs when the open score isn't in your Jubelle
//! Projects. Adding copies the file in; your original is never moved or changed.
Rectangle {
    id: root

    property alias navigationPanel: navPanel

    visible: barModel.shown
    implicitHeight: visible ? row.implicitHeight + 12 : 0
    color: Qt.rgba(0.659, 0.333, 0.969, 0.12)   // Jubelle purple, light

    NotInProjectsBarModel {
        id: barModel
    }

    Component.onCompleted: barModel.load()

    NavigationPanel {
        id: navPanel
        name: "JubelleNotInProjectsBar"
        direction: NavigationPanel.Horizontal
        enabled: root.visible
    }

    RowLayout {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 12

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            elide: Text.ElideMiddle
            text: qsTrc("project", "“%1” isn't in your Jubelle Projects.").arg(barModel.fileName)
        }
        FlatButton {
            text: qsTrc("project", "Add to Projects…")
            accentButton: true
            navigation.panel: navPanel
            navigation.column: 0
            onClicked: barModel.addToProjects()
        }
        FlatButton {
            text: qsTrc("project", "Not now")
            navigation.panel: navPanel
            navigation.column: 1
            onClicked: barModel.notNow()
        }
        FlatButton {
            text: qsTrc("project", "Don't ask for this file")
            transparent: true
            navigation.panel: navPanel
            navigation.column: 2
            onClicked: barModel.dontAskForThisFile()
        }
    }
}
