/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

Item {
    id: root

    property string currentPageName: ""
    property bool iconsOnly: false

    signal selected(string name)

    NavigationSection {
        id: navSec
        name: "HomeMenuSection"
        enabled: root.enabled && root.visible
        order: 2
    }

    NavigationPanel {
        id: navPanel
        name: "HomeMenuPanel"
        enabled: root.enabled && root.visible
        section: navSec
        order: 1
        direction: NavigationPanel.Vertical

        accessible.name: qsTrc("appshell", "Home menu") + " " + navPanel.directionInfo
    }

    // Jubelle: Home sidebar. Projects, Extensions, Template Library and Learning Center,
    // with Settings pinned at the bottom. Account, MuseSounds and Learn were removed.
    readonly property var mainSections: [
        { "name": "scores", "title": qsTrc("appshell", "Projects"), "iconCode": IconCode.OPEN_FILE },
        { "name": "extensions", "title": qsTrc("appshell", "Extensions"), "iconCode": IconCode.PLUGIN },
        { "name": "templates", "title": qsTrc("appshell", "Template Library"), "iconCode": IconCode.NEW_FILE },
        { "name": "learning", "title": qsTrc("appshell", "Learning Center"), "iconCode": IconCode.GRADUATION_CAP }
    ]

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 20
        anchors.bottomMargin: 16

        spacing: 0

        RadioButtonGroup {
            id: radioButtonList

            Layout.fillHeight: true
            Layout.fillWidth: true

            orientation: ListView.Vertical
            spacing: 4

            model: root.mainSections

            currentIndex: 0

            delegate: SidebarButton {
                required property int index
                required property var modelData

                width: radioButtonList.width

                sectionName: modelData.name
                title: modelData.title
                sectionIconCode: modelData.iconCode
                navigationRow: 1 + index
            }
        }

        SeparatorLine {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 8
        }

        SidebarButton {
            Layout.fillWidth: true

            sectionName: "settings"
            title: qsTrc("appshell", "Settings")
            sectionIconCode: IconCode.SETTINGS_COG
            navigationRow: 1 + root.mainSections.length
        }
    }

    component SidebarButton: PageTabButton {
        id: button

        property string sectionName: ""
        property int sectionIconCode: IconCode.NONE
        property int navigationRow: 0

        navigation.name: title
        navigation.panel: navPanel
        navigation.row: navigationRow

        height: 44
        spacing: 14
        leftPadding: 18

        ButtonGroup.group: radioButtonList.radioButtonGroup
        orientation: Qt.Horizontal
        checked: sectionName === root.currentPageName

        iconOnly: root.iconsOnly

        iconComponent: StyledIconLabel {
            iconCode: button.sectionIconCode
        }

        onToggled: {
            root.selected(sectionName)
        }
    }
}
