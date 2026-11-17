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

//! Jubelle R6: the "Repeat playback" heading on every repeat element (repeat
//! barlines, voltas, jumps, markers), with the way to the whole-score overview.
//! Playback only; the printed repeats never change.
Column {
    id: root

    property var model: null
    property string note: ""
    property NavigationPanel navigationPanel: null
    property int navigationRowStart: 0
    readonly property int navigationRowEnd: navigationRowStart + 1

    width: parent ? parent.width : 0
    spacing: 6

    StyledTextLabel {
        width: parent.width
        horizontalAlignment: Text.AlignLeft
        font: ui.theme.bodyBoldFont
        color: "#A855F7"
        text: qsTrc("inspector", "Repeat playback")
    }

    StyledTextLabel {
        width: parent.width
        visible: root.note !== ""
        horizontalAlignment: Text.AlignLeft
        wrapMode: Text.Wrap
        opacity: 0.75
        text: root.note
    }

    FlatButton {
        width: parent.width
        text: qsTrc("inspector", "All repeats in the score…")
        navigation.name: "JubelleAllRepeats"
        navigation.panel: root.navigationPanel
        navigation.row: root.navigationRowStart
        onClicked: {
            if (root.model) {
                root.model.jubelleOpenRepeatsDialog()
            }
        }
    }
}
