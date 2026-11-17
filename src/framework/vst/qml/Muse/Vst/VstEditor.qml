/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
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
import QtQuick

import Muse.Ui
import Muse.UiComponents
import Muse.Vst

Rectangle {
    id: root

    // in
    property alias instanceId: view.instanceId

    // out
    property alias title: view.title

    color: ui.theme.backgroundPrimaryColor

    implicitWidth: Math.max(view.implicitWidth, presetBar.implicitWidth + 24)
    // Jubelle: the plugin's own window sits below the Presets bar
    implicitHeight: view.implicitHeight + view.topPadding

    Component.onCompleted: {
        view.init()
    }

    // Jubelle: Presets bar. Lists this plugin's .vstpreset files (from the
    // standard VST3 preset folders) and loads one; the result saves with the
    // score like any other change to the plugin's settings.
    VstPresetsModel {
        id: presetsModel
        instanceId: view.instanceId
    }

    Row {
        id: presetBar
        anchors.top: parent.top
        anchors.topMargin: 8
        anchors.left: parent.left
        anchors.leftMargin: 12
        height: 32
        spacing: 8

        StyledTextLabel {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTrc("vst", "Preset")
        }

        StyledDropdown {
            id: presetDropdown
            width: 260
            anchors.verticalCenter: parent.verticalCenter
            enabled: presetsModel.presets.length > 0
            model: presetsModel.presets
            textRole: "title"
            valueRole: "path"
            currentIndex: -1
            displayText: presetsModel.currentPresetTitle !== "" ? presetsModel.currentPresetTitle
                                                                : (presetsModel.presets.length > 0 ? qsTrc("vst", "Choose a preset…")
                                                                                                   : qsTrc("vst", "No preset files"))
            onActivated: function(index, value) {
                presetsModel.applyPreset(index)
            }
        }

        FlatButton {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTrc("vst", "Open preset file…")
            onClicked: presetsModel.openPresetFile()
        }

        StyledTextLabel {
            anchors.verticalCenter: parent.verticalCenter
            visible: text !== ""
            text: presetsModel.message
            opacity: 0.75
        }
    }

    VstView {
        id: view
        anchors.fill: parent
        topPadding: presetBar.height + 16
    }
}
