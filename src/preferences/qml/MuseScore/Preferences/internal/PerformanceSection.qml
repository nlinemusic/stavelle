/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle: performance profile. It sets the audio buffer, the SoundFont filter and how
// often scores are saved automatically; Automatic chooses for this computer.

import QtQuick

import Muse.Ui
import Muse.UiComponents

BaseSection {
    id: root

    title: qsTrc("preferences", "Performance")

    property var profiles: []
    property int currentProfile: 0
    property string detail: ""

    signal profileSelected(int profile)

    StyledDropdown {
        id: dropdown
        width: root.columnWidth * 1.6

        model: root.profiles
        currentIndex: dropdown.indexOfValue(root.currentProfile)

        navigation.name: "PerformanceProfileBox"
        navigation.panel: root.navigation
        navigation.order: 1

        onActivated: function(index, value) {
            root.profileSelected(value)
        }
    }

    StyledTextLabel {
        width: parent.width
        horizontalAlignment: Text.AlignLeft
        wrapMode: Text.WordWrap
        opacity: 0.75
        text: root.detail
    }
}
