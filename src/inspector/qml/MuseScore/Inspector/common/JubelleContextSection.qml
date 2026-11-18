/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle: contextual Properties for Jubelle's own items (recorded tempo
// points, timeline markers, page breaks placed by Page turns, dynamics, the
// swing marking). See jubellecontextsettingsmodel.h.

import QtQuick

import Muse.Ui
import Muse.UiComponents
import MuseScore.Inspector

Column {
    id: root

    property JubelleContextSettingsModel model: null
    property NavigationPanel navigationPanel: null
    property int navigationRowStart: 0

    width: parent ? parent.width : 0
    spacing: 8

    StyledTextLabel {
        width: parent.width
        horizontalAlignment: Text.AlignLeft
        font: ui.theme.bodyBoldFont
        text: root.model ? root.model.heading : ""
    }

    StyledTextLabel {
        width: parent.width
        horizontalAlignment: Text.AlignLeft
        wrapMode: Text.WordWrap
        opacity: 0.75
        text: root.model ? root.model.detail : ""
    }

    // recorded tempo point
    Row {
        visible: root.model && root.model.kind === "tempo"
        spacing: 8

        StyledTextLabel {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTrc("inspector", "Tempo")
        }

        IncrementalPropertyControl {
            width: 110
            currentValue: root.model ? root.model.tempoBpm : 0
            minValue: 10
            maxValue: 400
            step: 1
            decimals: 1
            measureUnitsSymbol: qsTrc("inspector", "BPM")
            navigation.name: "JubelleTempoValue"
            navigation.panel: root.navigationPanel
            navigation.row: root.navigationRowStart
            onValueEdited: function(v) { root.model.setTempoBpm(v) }
        }
    }

    // timeline marker
    TextInputField {
        width: parent.width
        visible: root.model && root.model.kind === "marker"
        currentText: root.model ? root.model.markerName : ""
        hint: qsTrc("inspector", "Marker name")
        navigation.name: "JubelleMarkerName"
        navigation.panel: root.navigationPanel
        navigation.row: root.navigationRowStart + 1
        onTextEditingFinished: function(t) { root.model.setMarkerName(t) }
    }

    Row {
        spacing: 8

        FlatButton {
            visible: root.model && root.model.kind === "marker"
            text: qsTrc("inspector", "Play from here")
            icon: IconCode.PLAY
            orientation: Qt.Horizontal
            navigation.name: "JubellePlayFromHere"
            navigation.panel: root.navigationPanel
            navigation.row: root.navigationRowStart + 2
            onClicked: root.model.playFromHere()
        }

        FlatButton {
            visible: root.model && root.model.kind === "pagebreak"
            text: qsTrc("inspector", "Keep as my own break")
            navigation.name: "JubelleKeepBreak"
            navigation.panel: root.navigationPanel
            navigation.row: root.navigationRowStart + 3
            onClicked: root.model.keepAsOwnBreak()
        }

        FlatButton {
            visible: root.model && (root.model.kind === "tempo" || root.model.kind === "marker" || root.model.kind === "pagebreak")
            text: root.model && root.model.kind === "marker" ? qsTrc("inspector", "Delete") : qsTrc("inspector", "Remove")
            navigation.name: "JubelleRemove"
            navigation.panel: root.navigationPanel
            navigation.row: root.navigationRowStart + 4
            onClicked: root.model.removeItem()
        }

        FlatButton {
            visible: root.model && root.model.kind === "swing"
            text: qsTrc("inspector", "Swing…")
            navigation.name: "JubelleSwing"
            navigation.panel: root.navigationPanel
            navigation.row: root.navigationRowStart + 5
            onClicked: root.model.openSwing()
        }
    }
}
