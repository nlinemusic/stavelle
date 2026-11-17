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
import MuseScore.Preferences

//! Jubelle: Settings on the Home screen. It shows the same settings as the
//! Preferences window (one shared implementation, PreferencesContent).
FocusScope {
    id: root

    NavigationSection {
        id: navSec
        name: "Settings"
        enabled: root.enabled && root.visible
        order: 3
        onActiveChanged: function(active) {
            if (active) {
                root.forceActiveFocus()
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: ui.theme.backgroundSecondaryColor
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 36
        anchors.leftMargin: 36
        anchors.rightMargin: 36
        anchors.bottomMargin: 12
        spacing: 16

        StyledTextLabel {
            text: qsTrc("appshell", "Settings")
            font: ui.theme.titleBoldFont
            horizontalAlignment: Text.AlignLeft
        }

        Loader {
            id: contentLoader

            Layout.fillWidth: true
            Layout.fillHeight: true

            sourceComponent: contentComp

            //! "OK" keeps the changes, "Cancel" reverts them. In both cases the
            //! page is rebuilt so it shows the settings as they now are.
            function reload() {
                sourceComponent = null
                sourceComponent = contentComp
            }
        }
    }

    Component {
        id: contentComp

        PreferencesContent {
            navigationSection: navSec

            onHideRequested: Qt.callLater(contentLoader.reload)
            onRejectRequested: Qt.callLater(contentLoader.reload)
        }
    }
}
