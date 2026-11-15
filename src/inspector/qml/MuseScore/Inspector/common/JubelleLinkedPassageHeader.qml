/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

//! Jubelle: the "Linked passage" section, shown when a linked passage's marker
//! is selected (View › Show › Show invisible shows the markers). It says what
//! the passage follows or leads and what is copied, and offers the same
//! commands as the marker's right-click menu.
Column {
    id: root

    property var model: null
    property NavigationPanel navigationPanel: null
    property int navigationRowStart: 0

    width: parent ? parent.width : 0
    spacing: 8

    StyledTextLabel {
        width: parent.width
        horizontalAlignment: Text.AlignLeft
        font: ui.theme.bodyBoldFont
        color: "#A855F7"
        text: qsTrc("inspector", "Linked passage")
    }

    StyledTextLabel {
        width: parent.width
        horizontalAlignment: Text.AlignLeft
        wrapMode: Text.Wrap
        opacity: 0.85
        text: root.model ? root.model.jubelleLinkSummary : ""
    }

    RowLayout {
        width: parent.width
        spacing: 8

        FlatButton {
            Layout.fillWidth: true
            visible: root.model && !root.model.jubelleIsLinkSource
            text: qsTrc("inspector", "Re-sync")
            toolTipTitle: qsTrc("inspector", "Re-sync")
            toolTipDescription: qsTrc("inspector", "Bring this passage back in line with the one it follows, including notes you changed by hand. Your pitches and octave are kept.")
            navigation.name: "JubelleLinkResync"
            navigation.panel: root.navigationPanel
            navigation.row: root.navigationRowStart
            onClicked: root.model.jubelleLinkAction("jubelle-link-resync")
        }

        FlatButton {
            Layout.fillWidth: true
            text: qsTrc("inspector", "Unlink")
            toolTipTitle: qsTrc("inspector", "Unlink")
            toolTipDescription: qsTrc("inspector", "Stop linking this passage. The music stays as it is; the other passages stay linked.")
            navigation.name: "JubelleLinkUnlink"
            navigation.panel: root.navigationPanel
            navigation.row: root.navigationRowStart + 1
            onClicked: root.model.jubelleLinkAction("jubelle-link-unlink")
        }
    }

    FlatButton {
        width: parent.width
        text: qsTrc("inspector", "All linked passages…")
        navigation.name: "JubelleAllLinkedPassages"
        navigation.panel: root.navigationPanel
        navigation.row: root.navigationRowStart + 2
        onClicked: root.model.jubelleLinkAction("jubelle-linked-passages")
    }

    SeparatorLine { }
}
