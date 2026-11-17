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

//! Jubelle R4 (approved mockup): the Home screen's top bar. The logo and name,
//! the sections as tabs, and Settings on the right. Replaces MuseScore's left
//! Home sidebar; the sections are the same.
Rectangle {
    id: root

    property string currentPageName: ""
    signal selected(string name)

    implicitHeight: 60
    color: ui.theme.backgroundPrimaryColor

    NavigationSection {
        id: navSec
        name: "HomeHeaderSection"
        enabled: root.enabled && root.visible
        order: 2
    }

    NavigationPanel {
        id: navPanel
        name: "HomeHeaderPanel"
        enabled: root.enabled && root.visible
        section: navSec
        order: 1
        direction: NavigationPanel.Horizontal
        accessible.name: qsTrc("appshell", "Home menu") + " " + navPanel.directionInfo
    }

    readonly property var sections: [
        { "name": "scores", "title": qsTrc("appshell", "Projects") },
        { "name": "templates", "title": qsTrc("appshell", "Templates") },
        { "name": "extensions", "title": qsTrc("appshell", "Plugins") },
        { "name": "learning", "title": qsTrc("appshell", "Learning Center") }
    ]

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        spacing: 6

        Image {
            Layout.preferredWidth: 30
            Layout.preferredHeight: 30
            source: "../resources/jubelle_logo.png"
            sourceSize: Qt.size(60, 60)
            smooth: true
            mipmap: true
        }

        StyledTextLabel {
            Layout.rightMargin: 22
            text: "Jubelle"
            font.family: ui.theme.bodyBoldFont.family
            font.pixelSize: 20
            font.bold: true
        }

        Repeater {
            model: root.sections

            delegate: FlatButton {
                required property var modelData
                required property int index

                readonly property bool isCurrent: modelData.name === root.currentPageName

                text: modelData.title
                transparent: !isCurrent
                textFont: isCurrent ? ui.theme.bodyBoldFont : ui.theme.bodyFont
                normalColor: isCurrent ? ui.theme.popupBackgroundColor : "transparent"

                navigation.name: modelData.name
                navigation.panel: navPanel
                navigation.column: index

                onClicked: root.selected(modelData.name)
            }
        }

        Item { Layout.fillWidth: true }

        FlatButton {
            icon: IconCode.SETTINGS_COG
            transparent: root.currentPageName !== "settings"
            toolTipTitle: qsTrc("appshell", "Settings")
            navigation.name: "settings"
            navigation.panel: navPanel
            navigation.column: root.sections.length
            onClicked: root.selected("settings")
        }
    }

    SeparatorLine {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }
}
