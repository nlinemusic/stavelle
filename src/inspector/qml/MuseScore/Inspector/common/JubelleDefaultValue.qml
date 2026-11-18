/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle, based on MuseScore Studio
 * Copyright (C) 2026 MuseScore Limited and others
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

// Jubelle: the shared "local value / default / Use default" line for contextual
// Properties. Shows where the current value comes from; when a value is set on
// the selection itself it also shows what the default would be and offers
// "Use default", which clears the local value.

import QtQuick

import Muse.Ui
import Muse.UiComponents

Column {
    id: root

    //! true when the value is set on the selection itself
    property bool isSet: false
    //! where the current value comes from ("set here", "from whole score", ...)
    property string sourceText: ""
    //! e.g. "Default: 100% (whole score)"; shown only when isSet
    property string defaultText: ""
    property color highlightColor: ui.theme.accentColor

    property NavigationPanel navigationPanel: null
    property int navigationRow: 0
    property string navigationName: "UseDefault"

    signal useDefaultRequested()

    spacing: 2

    Row {
        width: parent.width
        spacing: 6

        StyledTextLabel {
            width: parent.width - useDefaultButton.width - parent.spacing
            anchors.verticalCenter: parent.verticalCenter
            horizontalAlignment: Text.AlignLeft
            elide: Text.ElideRight
            color: root.isSet ? root.highlightColor : ui.theme.fontPrimaryColor
            opacity: root.isSet ? 1.0 : 0.6
            text: root.sourceText
        }

        FlatButton {
            id: useDefaultButton
            text: qsTrc("inspector", "Use default")
            visible: root.isSet
            navigation.name: root.navigationName
            navigation.panel: root.navigationPanel
            navigation.row: root.navigationRow
            navigation.accessible.name: qsTrc("inspector", "Use default") + (root.defaultText !== "" ? ", " + root.defaultText : "")
            onClicked: root.useDefaultRequested()
        }
    }

    StyledTextLabel {
        width: parent.width
        visible: root.isSet && root.defaultText !== ""
        horizontalAlignment: Text.AlignLeft
        wrapMode: Text.Wrap
        opacity: 0.7
        text: root.defaultText
    }
}
