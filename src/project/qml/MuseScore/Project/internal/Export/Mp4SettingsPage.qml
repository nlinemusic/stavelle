/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2025 MuseScore Limited and others
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

import Muse.UiComponents
import Muse.Ui
import MuseScore.Project

ExportSettingsPage {
    id: root

    property bool isFFmpegAvailable: Boolean(root.model) && root.model.isFFmpegAvailable

    isExportAvailable: root.isFFmpegAvailable

    Loader {
        width: parent.width

        sourceComponent: root.isFFmpegAvailable ? videoSettingsComp : ffmpegMissingComp
    }

    Component {
        id: ffmpegMissingComp

        Column {
            width: parent.width
            spacing: 12

            StyledTextLabel {
                width: parent.width
                text: qsTrc("project/export", "To export to this file format, an FFmpeg installation is required. You can also configure this in Preferences > Video.")
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
            }

            FlatButton {
                id: downloadButton

                text: qsTrc("project/export", "Download FFmpeg")
                icon: IconCode.OPEN_LINK
                isNarrow: true
                orientation: Qt.Horizontal

                navigation.name: "ExportDownloadFFmpeg"
                navigation.panel: root.navigationPanel
                navigation.row: root.navigationOrder

                onClicked: {
                    api.launcher.openUrl("https://handbook.musescore.org/video/installing-ffmpeg")
                }
            }

            FilePicker {
                width: parent.width

                pickerType: FilePicker.PickerType.Directory
                dialogTitle: qsTrc("project/export", "Choose FFmpeg library folder")

                buttonType: FlatButton.TextOnly
                buttonText: qsTrc("project/export", "Locate existing installation")
                showPathField: false

                dir: root.model ? root.model.ffmpegDir : ""
                path: root.model ? root.model.ffmpegDir : ""

                navigation: root.navigationPanel
                navigationRowOrderStart: downloadButton.navigation.order + 1

                onPathEdited: function(newPath) {
                    if (root.model) {
                        root.model.ffmpegDir = newPath
                    }
                }
            }
        }
    }

    Component {
        id: videoSettingsComp

        Column {
            width: parent.width
            spacing: 12

            AudioSettings {
                id: audioSection

                model: root.model
                navigationPanel: root.navigationPanel
                navigationOrderStart: root.navigationOrder
                // Jubelle: the video has its own sound quality below, separate from MP3 export
                showBitRateControl: false
            }

            ExportOptionItem {
                id: videoAudioLabel
                text: qsTrc("project/export", "Sound quality (AAC):")

                StyledDropdown {
                    id: videoAudioDropdown
                    Layout.preferredWidth: 126

                    navigation.name: "VideoAudioBitrateDropdown"
                    navigation.panel: root.navigationPanel
                    navigation.row: audioSection.navigationOrderEnd + 1
                    navigation.accessible.name: videoAudioLabel.text + " " + currentText

                    model: root.model ? root.model.availableVideoAudioBitrates().map(function(rate) {
                        return { text: qsTrc("project/export", "%1 kBit/s").arg(rate), value: rate }
                    }) : []

                    currentIndex: root.model ? indexOfValue(root.model.videoAudioBitrate) : -1

                    onActivated: function(index, value) {
                        root.model.videoAudioBitrate = value
                    }
                }
            }

            ExportOptionItem {
                id: videoResolutionLabel
                text: qsTrc("project/export", "Video resolution")

                StyledDropdown {
                    id: videoResolutionDropdown
                    Layout.preferredWidth: 126

                    navigation.name: "VideoResolutionDropdown"
                    navigation.panel: root.navigationPanel
                    navigation.row: audioSection.navigationOrderEnd + 2
                    navigation.accessible.name: videoResolutionLabel.text + " " + currentText

                    model: root.model ? root.model.availableVideoResolutions().map(function(res) {
                        return { text: res, value: res }
                    }) : []

                    currentIndex: root.model ? indexOfValue(root.model.videoResolution) : -1

                    onActivated: function(index, value) {
                        root.model.videoResolution = value
                    }
                }
            }

            RadioButtonGroup {
                orientation: ListView.Vertical
                spacing: 8
                width: parent.width

                model: root.model ? root.model.availableViewModes().map(function(obj) {
                    return { text: obj.text, value: obj.value }
                }) : []

                delegate: RoundedRadioButton {
                    required property var modelData
                    required property int index

                    text: modelData.text
                    checked: root.model.viewMode === modelData.value
                    onToggled: root.model.viewMode = modelData.value

                    navigation.name: modelData.text
                    navigation.panel: root.navigationPanel
                    navigation.row: videoResolutionDropdown.navigation.row + 1 + index
                    navigation.accessible.name: modelData.text
                }
            }

            CheckBox {
                width: parent.width
                text: qsTrc("project/export", "Show playback cursor")
                checked: root.model ? root.model.videoShowCursor : false
                navigation.name: "VideoShowCursor"
                navigation.panel: root.navigationPanel
                navigation.row: videoResolutionDropdown.navigation.row + 8
                onClicked: root.model.videoShowCursor = !checked
            }

            StyledTextLabel {
                width: parent.width
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: qsTrc("project/export", "The video shows still pages on white paper, as printed. It begins on the first page, silent for the lead-in, with no title screen.")
            }

            // Jubelle: which pages, in which order
            VideoPageSequence {
                width: parent.width
                model: root.model
                navigationPanel: root.navigationPanel
                navigationRowStart: videoResolutionDropdown.navigation.row + 10
            }

            StyledTextLabel {
                width: parent.width
                text: qsTrc("project/export", "Each selected part will be exported as a separate video file.")
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
            }
        }
    }
}
