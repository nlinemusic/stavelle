/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 * Music Composition & Notation
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
import MuseScore.Project

import "internal/NewScore"

//! Jubelle: Template Library. Browse the built-in and user templates (the same
//! list as the New Score dialog) and start a new score from the chosen one.
FocusScope {
    id: root

    property NavigationSection navigationSection: NavigationSection {
        name: "TemplateLibrary"
        enabled: root.enabled && root.visible
        order: 3
        onActiveChanged: function(active) {
            if (active) {
                root.forceActiveFocus()
            }
        }
    }

    ProjectsPageModel {
        id: pageModel
    }

    function createFromSelected() {
        if (browser.hasSelectedTemplate) {
            pageModel.createScoreFromTemplate(browser.selectedTemplatePath)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 36
        spacing: 20

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Column {
                Layout.fillWidth: true
                spacing: 6

                StyledTextLabel {
                    text: qsTrc("project", "Template Library")
                    font: ui.theme.titleBoldFont
                    horizontalAlignment: Text.AlignLeft
                }

                StyledTextLabel {
                    width: parent.width
                    text: qsTrc("project", "Browse templates and start a new score from one. Double-click a template to start right away.")
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.Wrap
                    opacity: 0.75
                }
            }

            FlatButton {
                text: qsTrc("project", "New score from template")
                accentButton: true
                enabled: browser.hasSelectedTemplate

                navigation.name: "NewScoreFromTemplate"
                navigation.panel: buttonsPanel
                navigation.order: 1

                onClicked: root.createFromSelected()
            }
        }

        NavigationPanel {
            id: buttonsPanel
            name: "TemplateLibraryButtons"
            section: root.navigationSection
            order: 1
        }

        CreateFromTemplatePage {
            id: browser

            Layout.fillWidth: true
            Layout.fillHeight: true

            navigationSection: root.navigationSection

            onDone: root.createFromSelected()
        }
    }
}
