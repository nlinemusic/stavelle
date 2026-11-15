/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021-2024 MuseScore Limited and others
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

import MuseScore.AppShell

StyledDialogView {
    id: root

    title: qsTrc("appshell/about", "About Jubelle")

    contentHeight: showFonts ? 700 : 470
    contentWidth: 520

    property bool showFonts: false

    AboutModel {
        id: aboutModel
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 30

        ColumnLayout {
            id: content

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 36
            Layout.leftMargin: 40
            Layout.rightMargin: 40

            spacing: 32

            Image {
                id: logo
                Layout.alignment: Qt.AlignHCenter

                // Jubelle: the approved Jubelle icon
                source: "resources/jubelle_logo.png"
                sourceSize: Qt.size(100, 100)
                Layout.preferredWidth: 100
                Layout.preferredHeight: 100
                smooth: true
                mipmap: true

                MouseArea {
                    anchors.fill: parent

                    property int clickCount: 0

                    onClicked: {
                        clickCount++

                        if (clickCount % 3 == 0) {
                            aboutModel.toggleDevMode()
                        }
                    }
                }
            }

            Column {
                spacing: 8
                Layout.fillWidth: true

                StyledTextLabel {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTrc("appshell/about", "Version:") + " " + aboutModel.museScoreVersion()
                    font: ui.theme.bodyBoldFont
                }

                Row {
                    spacing: 4
                    anchors.horizontalCenter: parent.horizontalCenter

                    StyledTextLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTrc("appshell/about", "Revision:") + " " + aboutModel.museScoreRevision()
                    }

                    FlatButton {
                        anchors.verticalCenter: parent.verticalCenter
                        icon: IconCode.COPY

                        onClicked: {
                            aboutModel.copyRevisionToClipboard()
                        }
                    }
                }
            }

            StyledTextLabel {
                Layout.fillWidth: true
                //: Jubelle: where new versions come from, and what it's based on
                text: qsTrc("appshell/about", "New versions: <a href=\"%1\">Jubelle releases on GitHub</a>.<br>Jubelle is based on <a href=\"%2\">MuseScore Studio</a>.")
                      .arg("https://github.com/nlinemusic/stavelle/releases")
                      .arg(aboutModel.museScoreUrl().url)
                wrapMode: Text.WordWrap
                maximumLineCount: 3
            }

            StyledTextLabel {
                Layout.fillWidth: true
                text: qsTrc("appshell/about", "Jubelle sends nothing about you or your scores. It checks for updates or sends a crash report only when you choose to.")
                wrapMode: Text.WordWrap
                maximumLineCount: 3
            }

            StyledTextLabel {
                Layout.fillWidth: true
                text: qsTrc("appshell/about", "Copyright © 1999-2026 MuseScore Limited and others.\nPublished under the <a href=\"%1\">GNU General Public License version 3</a>.")
                      .arg("https://www.gnu.org/licenses/gpl-3.0.html")
                      .replace("\n", "<br>")

                wrapMode: Text.WordWrap
                maximumLineCount: 3
            }

            // Jubelle: the music and text fonts that come with Jubelle, and their licences
            FlatButton {
                Layout.alignment: Qt.AlignHCenter
                text: root.showFonts ? qsTrc("appshell/about", "Hide fonts") : qsTrc("appshell/about", "Fonts…")
                onClicked: root.showFonts = !root.showFonts
            }

            StyledTextLabel {
                Layout.fillWidth: true
                visible: root.showFonts
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                textFormat: Text.RichText
                text: [
                    "<b>Leland</b>, Leland Text, MuseJazz, MuseJazz Text, Edwin: MuseScore, SIL Open Font License 1.1",
                    "<b>Bravura</b>, Petaluma: Steinberg Media Technologies, SIL Open Font License 1.1",
                    "<b>Finale Maestro</b>, Finale Broadway: MakeMusic, SIL Open Font License 1.1",
                    "<b>Sebastian</b>: Florian Kretlow and Ben Byram-Wigfield, SIL Open Font License 1.1",
                    "<b>Leipzig</b>: Etienne Darbellay, Jean-François Marti, Laurent Pugin and Klaus Rettinghaus (Verovio), SIL Open Font License 1.1",
                    "<b>Eugene</b>: Mikko Patama, SIL Open Font License 1.1",
                    "<b>Gootville</b>: Grzegorz Pruchniakowski, SIL Open Font License 1.1",
                    "<b>Campania</b>: Marc Sabatella, SIL Open Font License 1.1",
                    "<b>FreeSerif</b>, FreeSans: GNU FreeFont, GPL with font exception",
                    qsTrc("appshell/about", "The licence texts are in the fonts folder of Jubelle's source code and installation.")
                ].join("<br>")
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            Layout.rightMargin: 16
            Layout.bottomMargin: 16

            spacing: 12

            FlatButton {
                text: qsTrc("global", "OK")

                onClicked: {
                    root.hide()
                }
            }
        }
    }
}
