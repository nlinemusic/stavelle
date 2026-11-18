/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle: the video's page-sequence builder. Pick pages from the strip,
// repeat or remove entries, change their order, read the plain summary and
// preview the first seconds of each entry. The text box stays as a shortcut
// ("2-4", "1, 3, 1"). The same engine as the export works out every length.

import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

Column {
    id: root

    property ExportDialogModel model: null
    property NavigationPanel navigationPanel: null
    property int navigationRowStart: 0
    //! thumbnails show the printed pages; with "Reflow" the video makes its own pages, so numbers only
    readonly property bool showThumbnails: Boolean(root.model) && root.model.viewMode === ExportDialogModel.PageFull

    spacing: 8

    StyledTextLabel {
        width: parent.width
        horizontalAlignment: Text.AlignLeft
        font: ui.theme.bodyBoldFont
        text: qsTrc("project/export", "Pages and order")
    }

    StyledTextLabel {
        width: parent.width
        horizontalAlignment: Text.AlignLeft
        wrapMode: Text.WordWrap
        opacity: 0.7
        text: qsTrc("project/export", "Click a page to add it to the end. Leave the list as it is for the whole piece.")
    }

    // The score's pages
    ListView {
        id: pageStrip
        width: parent.width
        height: root.showThumbnails ? 96 : 32
        orientation: ListView.Horizontal
        spacing: 6
        clip: true
        model: root.model ? root.model.videoPageCount : 0

        delegate: FlatButton {
            required property int index
            width: root.showThumbnails ? 64 : 40
            height: pageStrip.height
            transparent: root.showThumbnails
            text: root.showThumbnails ? "" : String(index + 1)
            toolTipTitle: qsTrc("project/export", "Add page %1").arg(index + 1)
            navigation.name: "VideoPageStrip" + index
            navigation.panel: root.navigationPanel
            navigation.row: root.navigationRowStart + 1
            navigation.column: index
            navigation.accessible.name: toolTipTitle

            Column {
                anchors.fill: parent
                visible: root.showThumbnails
                spacing: 2

                VideoPageThumbnail {
                    width: parent.width
                    height: parent.height - 18
                    pageNumber: index + 1
                }

                StyledTextLabel {
                    width: parent.width
                    text: String(index + 1)
                }
            }

            onClicked: root.model.videoSequenceAppend(index + 1)
        }
    }

    // The sequence
    Repeater {
        id: entries
        model: root.model ? root.model.videoSequence : []

        delegate: RowLayout {
            required property var modelData
            required property int index

            width: root.width
            spacing: 6

            VideoPageThumbnail {
                visible: root.showThumbnails
                Layout.preferredWidth: 28
                Layout.preferredHeight: 36
                pageNumber: modelData.page
            }

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                elide: Text.ElideRight
                opacity: modelData.hasMusic ? 1.0 : 0.6
                text: qsTrc("project/export", "%1. Page %2 · %3").arg(index + 1).arg(modelData.page).arg(modelData.lengthText)
            }

            FlatButton {
                icon: IconCode.ARROW_UP
                enabled: index > 0
                toolTipTitle: qsTrc("project/export", "Move earlier")
                navigation.name: "VideoSeqUp" + index
                navigation.panel: root.navigationPanel
                navigation.row: root.navigationRowStart + 2 + index
                navigation.column: 0
                onClicked: root.model.videoSequenceMove(index, index - 1)
            }

            FlatButton {
                icon: IconCode.ARROW_DOWN
                enabled: index < entries.count - 1
                toolTipTitle: qsTrc("project/export", "Move later")
                navigation.name: "VideoSeqDown" + index
                navigation.panel: root.navigationPanel
                navigation.row: root.navigationRowStart + 2 + index
                navigation.column: 1
                onClicked: root.model.videoSequenceMove(index, index + 1)
            }

            FlatButton {
                text: qsTrc("project/export", "Add again")
                toolTipTitle: qsTrc("project/export", "Show this page again straight after (its music is heard again)")
                navigation.name: "VideoSeqAgain" + index
                navigation.panel: root.navigationPanel
                navigation.row: root.navigationRowStart + 2 + index
                navigation.column: 2
                onClicked: root.model.videoSequenceRepeatAt(index)
            }

            FlatButton {
                text: qsTrc("project/export", "Remove")
                navigation.name: "VideoSeqRemove" + index
                navigation.panel: root.navigationPanel
                navigation.row: root.navigationRowStart + 2 + index
                navigation.column: 3
                onClicked: root.model.videoSequenceRemoveAt(index)
            }
        }
    }

    StyledTextLabel {
        width: parent.width
        horizontalAlignment: Text.AlignLeft
        wrapMode: Text.WordWrap
        visible: text !== ""
        text: root.model ? root.model.videoSequenceSummary : ""
    }

    RowLayout {
        width: parent.width
        spacing: 8

        FlatButton {
            icon: root.model && root.model.videoPreviewing ? IconCode.STOP : IconCode.PLAY
            text: root.model && root.model.videoPreviewing ? qsTrc("project/export", "Stop preview")
                                                         : qsTrc("project/export", "Preview")
            orientation: Qt.Horizontal
            toolTipTitle: qsTrc("project/export", "Plays the first seconds of each entry in turn")
            enabled: entries.count > 0
            navigation.name: "VideoSeqPreview"
            navigation.panel: root.navigationPanel
            navigation.row: root.navigationRowStart + 200
            onClicked: root.model.videoPreviewing ? root.model.videoPreviewStop() : root.model.videoPreviewStart()
        }

        FlatButton {
            text: qsTrc("project/export", "Use every page")
            enabled: root.model && root.model.videoPageList !== ""
            navigation.name: "VideoSeqAll"
            navigation.panel: root.navigationPanel
            navigation.row: root.navigationRowStart + 201
            onClicked: root.model.videoSequenceUseAllPages()
        }

        Item { Layout.fillWidth: true }
    }

    // Shortcut: type the list
    ExportOptionItem {
        id: pagesLabel
        width: parent.width
        text: qsTrc("project/export", "Or type:")

        TextInputField {
            Layout.preferredWidth: 160
            hint: qsTrc("project/export", "All pages")
            currentText: root.model ? root.model.videoPageList : ""
            onTextEditingFinished: function(newText) {
                root.model.videoPageList = newText
            }
            navigation.name: "VideoPagesField"
            navigation.panel: root.navigationPanel
            navigation.row: root.navigationRowStart + 202
            navigation.accessible.name: pagesLabel.text
        }
    }

    StyledTextLabel {
        width: parent.width
        horizontalAlignment: Text.AlignLeft
        wrapMode: Text.WordWrap
        opacity: root.model && root.model.videoPageListProblem !== "" ? 1.0 : 0.7
        color: root.model && root.model.videoPageListProblem !== "" ? "#E5484D" : ui.theme.fontPrimaryColor
        text: root.model && root.model.videoPageListProblem !== ""
              ? root.model.videoPageListProblem
              : qsTrc("project/export", "“2-4” shows pages 2 to 4; “1, 3, 1” shows page 1 again after page 3. The sound follows the pages, with a short crossfade wherever music is skipped or repeated.")
    }
}
