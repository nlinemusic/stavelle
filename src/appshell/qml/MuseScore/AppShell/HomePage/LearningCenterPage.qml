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

//! Jubelle: Learning Center placeholder. The Learning Center itself is planned
//! for the final phase; this page only says it is coming.
FocusScope {
    id: root

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

    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - 72, 480)
        spacing: 16

        StyledIconLabel {
            anchors.horizontalCenter: parent.horizontalCenter
            iconCode: IconCode.GRADUATION_CAP
            font.pixelSize: 48
            color: "#A855F7"
        }

        StyledTextLabel {
            width: parent.width
            text: qsTrc("appshell", "Learning Center")
            font: ui.theme.titleBoldFont
        }

        StyledTextLabel {
            id: comingSoon

            width: parent.width
            text: qsTrc("appshell", "Coming soon. Guides, instrument ranges, transposition, shortcuts and tutorials will live here.")
            wrapMode: Text.Wrap
            opacity: 0.8

            NavigationFocusBorder { navigationCtrl: comingSoonNav }

            NavigationControl {
                id: comingSoonNav
                name: "ComingSoon"
                panel: navPanel
                order: 1
                accessible.role: MUAccessible.StaticText
                accessible.name: comingSoon.text
            }
        }
    }
}
