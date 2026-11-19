/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore). Help › Report a bug…
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.AppShell

StyledDialogView {
    id: root

    title: qsTrc("appshell/bugreport", "Report a bug")

    contentWidth: 640
    contentHeight: 680
    resizable: true

    property bool showingPreview: false

    BugReportModel {
        id: reportModel
    }

    NavigationPanel {
        id: fieldsPanel
        name: "BugReportFields"
        section: root.navigationSection
        order: 1
        direction: NavigationPanel.Vertical
    }

    component FieldLabel: StyledTextLabel {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignLeft
        font: ui.theme.bodyBoldFont
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        StyledFlickable {
            id: flick

            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.showingPreview
            contentHeight: form.implicitHeight
            clip: true

            ColumnLayout {
                id: form
                width: flick.width
                spacing: 8

                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.WordWrap
                    text: qsTrc("appshell/bugreport", "Describe what went wrong. Jubelle prepares a report on this computer that you can review and send yourself; nothing is uploaded.")
                }

                FieldLabel { text: qsTrc("appshell/bugreport", "Title") }
                TextInputField {
                    Layout.fillWidth: true
                    hint: qsTrc("appshell/bugreport", "A short summary, e.g. “Slur disappears after undo”")
                    currentText: reportModel.title
                    navigation.panel: fieldsPanel
                    navigation.order: 1
                    onTextChanged: function(newTextValue) { reportModel.title = newTextValue }
                }

                FieldLabel { text: qsTrc("appshell/bugreport", "What happened") }
                TextInputArea {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 70
                    currentText: reportModel.description
                    navigation.panel: fieldsPanel
                    navigation.order: 2
                    onTextChanged: function(newTextValue) { reportModel.description = newTextValue }
                }

                FieldLabel { text: qsTrc("appshell/bugreport", "Steps to reproduce") }
                TextInputArea {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 70
                    hint: qsTrc("appshell/bugreport", "1. … 2. … 3. …")
                    currentText: reportModel.steps
                    navigation.panel: fieldsPanel
                    navigation.order: 3
                    onTextChanged: function(newTextValue) { reportModel.steps = newTextValue }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        FieldLabel { text: qsTrc("appshell/bugreport", "Expected result") }
                        TextInputArea {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 56
                            currentText: reportModel.expected
                            navigation.panel: fieldsPanel
                            navigation.order: 4
                            onTextChanged: function(newTextValue) { reportModel.expected = newTextValue }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        FieldLabel { text: qsTrc("appshell/bugreport", "Actual result") }
                        TextInputArea {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 56
                            currentText: reportModel.actual
                            navigation.panel: fieldsPanel
                            navigation.order: 5
                            onTextChanged: function(newTextValue) { reportModel.actual = newTextValue }
                        }
                    }
                }

                FieldLabel { text: qsTrc("appshell/bugreport", "Comments (optional)") }
                TextInputArea {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    currentText: reportModel.comments
                    navigation.panel: fieldsPanel
                    navigation.order: 6
                    onTextChanged: function(newTextValue) { reportModel.comments = newTextValue }
                }

                FieldLabel { text: qsTrc("appshell/bugreport", "Screenshots (optional, up to 5)") }
                RowLayout {
                    spacing: 8
                    FlatButton {
                        text: qsTrc("appshell/bugreport", "Add picture…")
                        navigation.panel: fieldsPanel
                        navigation.order: 7
                        enabled: reportModel.screenshots.length < 5
                        onClicked: reportModel.addScreenshotFromFile()
                    }
                    FlatButton {
                        text: qsTrc("appshell/bugreport", "Capture Jubelle’s window")
                        navigation.panel: fieldsPanel
                        navigation.order: 8
                        enabled: reportModel.screenshots.length < 5
                        onClicked: reportModel.addScreenshotOfJubelle()
                    }
                }
                Repeater {
                    model: reportModel.screenshots
                    RowLayout {
                        spacing: 8
                        StyledTextLabel { text: modelData }
                        FlatButton {
                            icon: IconCode.DELETE_TANK
                            toolTipTitle: qsTrc("appshell/bugreport", "Remove")
                            navigation.panel: fieldsPanel
                            navigation.order: 20 + index
                            onClicked: reportModel.removeScreenshot(index)
                        }
                    }
                }

                CheckBox {
                    Layout.topMargin: 8
                    text: qsTrc("appshell/bugreport", "Include the end of Jubelle’s log and the last crash (cleaned)")
                    checked: reportModel.includeLogs
                    navigation.panel: fieldsPanel
                    navigation.order: 40
                    onClicked: reportModel.includeLogs = !checked
                }
                CheckBox {
                    text: reportModel.scoreAvailable
                          ? qsTrc("appshell/bugreport", "Include the open score (the last saved version)")
                          : qsTrc("appshell/bugreport", "Include the open score (save the score first)")
                    enabled: reportModel.scoreAvailable
                    checked: reportModel.includeScore
                    navigation.panel: fieldsPanel
                    navigation.order: 41
                    onClicked: reportModel.includeScore = !checked
                }

                FieldLabel { text: qsTrc("appshell/bugreport", "What is included") }
                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.WordWrap
                    text: {
                        reportModel.includeLogs; reportModel.includeScore; reportModel.screenshots
                        return reportModel.includedSummary()
                    }
                }
            }
        }

        // the whole report, exactly as it will be saved
        StyledFlickable {
            id: previewFlick
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.showingPreview
            contentHeight: previewText.implicitHeight
            clip: true

            TextEdit {
                id: previewText
                width: previewFlick.width
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                color: ui.theme.fontPrimaryColor
                font.family: "monospace"
                font.pixelSize: ui.theme.bodyFont.pixelSize
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            visible: reportModel.savedPath !== ""
            text: qsTrc("appshell/bugreport", "Saved: %1").arg(reportModel.savedPath)
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            visible: reportModel.problem !== "" && reportModel.savedPath === ""
            text: reportModel.problem
            color: ui.theme.accentColor
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            FlatButton {
                text: root.showingPreview ? qsTrc("appshell/bugreport", "Back to editing")
                                          : qsTrc("appshell/bugreport", "Preview report")
                onClicked: {
                    if (!root.showingPreview) {
                        previewText.text = reportModel.preview()
                    }
                    root.showingPreview = !root.showingPreview
                }
            }

            Item { Layout.fillWidth: true }

            FlatButton {
                text: qsTrc("appshell/bugreport", "Show in folder")
                visible: reportModel.savedPath !== ""
                onClicked: reportModel.showReportInFolder()
            }
            FlatButton {
                text: reportModel.savedPath !== "" ? qsTrc("global", "Close") : qsTrc("global", "Cancel")
                onClicked: root.hide()
            }
            FlatButton {
                text: qsTrc("appshell/bugreport", "Create report")
                accentButton: true
                enabled: reportModel.problem === "" && reportModel.savedPath === ""
                onClicked: reportModel.createReport()
            }
        }
    }
}
