/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
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

pragma ComponentBehavior: Bound

import QtQuick

import Muse.Ui
import Muse.UiComponents
import Muse.Dock

import MuseScore.Project

DockPage {
    id: root

    property string section: "scores"
    property string subSection: ""

    property var window: null

    objectName: "Home"
    uri: "musescore://home"

    onSetParamsRequested: function(params) {
        if (Boolean(params["section"])) {
            setCurrentCentral(params["section"])

            subSection = Boolean(params["subSection"]) ? params["subSection"] : ""
        }
    }

    onSectionChanged: {
        Qt.callLater(root.setCurrentCentral, section)
    }

    function setCurrentCentral(name) {
        // Jubelle: the MuseSounds, Learn and Account sections were removed from the Home
        // screen, so any request to open them shows Scores instead.
        if (name === "musesounds" || name === "learn" || name === "account") {
            name = "scores"
        }

        if (section === name || !Boolean(name)) {
            return
        }

        section = name

        switch (name) {
        case "scores": root.central = scoresComp; break
        case "plugins": root.central = extensionsComp; break // backward compatibility
        case "extensions": root.central = extensionsComp; break
        case "templates": root.central = templatesComp; break
        case "learning": root.central = learningComp; break
        case "settings": root.central = settingsComp; break
        }
    }

    // Jubelle R4 (approved mockup): the sections are tabs in a top bar
    // (HomeHeader) instead of MuseScore's left sidebar
    panels: []

    central: scoresComp

    function selectSection(name) {
        root.subSection = ""
        root.setCurrentCentral(name)
    }

    // Inline components do not share the file's scope, so the current section
    // and the tab clicks are passed through properties at each use.
    component WithHeader: Item {
        id: withHeader
        default property alias page: pageHolder.data
        property string currentPageName: ""
        signal sectionSelected(string name)

        HomeHeader {
            id: header
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            currentPageName: withHeader.currentPageName
            onSelected: function(name) {
                withHeader.sectionSelected(name)
            }
        }

        Item {
            id: pageHolder
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: header.bottom
            anchors.bottom: parent.bottom
        }
    }

    Component {
        id: scoresComp

        WithHeader {
            currentPageName: root.section
            onSectionSelected: function(name) { root.selectSection(name) }

            ScoresPage {
                anchors.fill: parent
                subSection: root.subSection

                onSubSectionApplied: {
                    root.subSection = ""
                }
            }
        }
    }

    Component {
        id: extensionsComp

        WithHeader {
            currentPageName: root.section
            onSectionSelected: function(name) { root.selectSection(name) }

            PluginsPage {
                anchors.fill: parent
            }
        }
    }

    Component {
        id: templatesComp

        WithHeader {
            currentPageName: root.section
            onSectionSelected: function(name) { root.selectSection(name) }

            TemplateLibraryPage {
                anchors.fill: parent
            }
        }
    }

    Component {
        id: learningComp

        WithHeader {
            currentPageName: root.section
            onSectionSelected: function(name) { root.selectSection(name) }

            LearningCenterPage {
                anchors.fill: parent
            }
        }
    }

    Component {
        id: settingsComp

        WithHeader {
            currentPageName: root.section
            onSectionSelected: function(name) { root.selectSection(name) }

            SettingsPage {
                anchors.fill: parent
            }
        }
    }

}
