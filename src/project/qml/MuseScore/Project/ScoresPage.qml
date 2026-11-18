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

//! Jubelle: the Projects page (Home → Projects). Recent first, then Projects with
//! categories, a tag filter (All / Any) and square project cards.
FocusScope {
    id: root

    property string subSection: ""

    signal subSectionApplied()

    QtObject {
        id: prv

        readonly property int sideMargin: 40
        readonly property int cardMinWidth: 196
        readonly property int cardSpacing: 20
        readonly property int recentCardWidth: 160

        readonly property string revealTitle: {
            switch (Qt.platform.os) {
            case "windows": return qsTrc("project", "Show in Explorer")
            case "osx": return qsTrc("project", "Show in Finder")
            default: return qsTrc("project", "Show in File Manager")
            }
        }
    }

    onSubSectionChanged: {
        if (root.subSection !== "") {
            // Jubelle: File › Import scores… opens this page and starts the import
            if (root.subSection === "import") {
                Qt.callLater(projectsModel.importScoresInteractive)
            }
            root.subSectionApplied()
        }
    }

    NavigationSection {
        id: navSec
        name: "ProjectsPage"
        enabled: root.enabled && root.visible
        order: 3
        onActiveChanged: {
            if (navSec.active) {
                root.forceActiveFocus()
            }
        }
    }

    ProjectsPageModel {
        id: projectsModel
    }

    Component.onCompleted: {
        projectsModel.load()
    }

    onVisibleChanged: {
        if (visible) {
            projectsModel.reload()
        }
    }

    Rectangle {
        id: background
        anchors.fill: parent
        color: ui.theme.backgroundSecondaryColor
    }

    NavigationPanel {
        id: topNavPanel
        name: "ProjectsTop"
        section: navSec
        order: 1
        direction: NavigationPanel.Horizontal
        accessible.name: qsTrc("project", "Projects")
    }

    StyledFlickable {
        id: flickable

        anchors.fill: parent
        contentWidth: width
        contentHeight: pageColumn.implicitHeight + 2 * prv.sideMargin

        Column {
            id: pageColumn

            x: prv.sideMargin
            y: prv.sideMargin
            width: flickable.width - 2 * prv.sideMargin
            spacing: 32

            // --- Search and New score ------------------------------------------------
            RowLayout {
                width: parent.width
                spacing: 12

                SearchField {
                    id: searchField

                    Layout.preferredWidth: 360
                    hint: qsTrc("project", "Search projects")

                    navigation.name: "ProjectsSearch"
                    navigation.panel: topNavPanel
                    navigation.column: 1

                    onSearchTextChanged: {
                        projectsModel.searchText = searchField.searchText
                    }
                }

                Item { Layout.fillWidth: true }

                // Jubelle R1: copy existing MuseScore files into Projects
                FlatButton {
                    text: qsTrc("project", "Import scores…")

                    navigation.name: "ImportScores"
                    navigation.panel: topNavPanel
                    navigation.column: 2

                    onClicked: projectsModel.importScoresInteractive()
                }

                FlatButton {
                    text: qsTrc("project", "Open other…")

                    navigation.name: "OpenOther"
                    navigation.panel: topNavPanel
                    navigation.column: 3

                    onClicked: projectsModel.openOther()
                }

                FlatButton {
                    text: qsTrc("project", "New score")
                    icon: IconCode.PLUS
                    orientation: Qt.Horizontal
                    accentButton: true

                    navigation.name: "NewScore"
                    navigation.panel: topNavPanel
                    navigation.column: 4

                    onClicked: projectsModel.createNewScore()
                }
            }

            // --- Recent ---------------------------------------------------------------
            Column {
                width: parent.width
                spacing: 14

                StyledTextLabel {
                    text: qsTrc("project", "Recent")
                    font: ui.theme.headerBoldFont
                }

                StyledTextLabel {
                    visible: recentRepeater.count === 0
                    text: qsTrc("project", "Scores you save in your projects appear here.")
                    opacity: 0.7
                }

                NavigationPanel {
                    id: recentNavPanel
                    name: "RecentScores"
                    section: navSec
                    order: 2
                    direction: NavigationPanel.Horizontal
                    accessible.name: qsTrc("project", "Recent")
                }

                StyledFlickable {
                    width: parent.width
                    height: recentRow.height
                    visible: recentRepeater.count > 0
                    contentWidth: recentRow.width
                    flickableDirection: Flickable.HorizontalFlick

                    Row {
                        id: recentRow
                        spacing: 16

                        Repeater {
                            id: recentRepeater
                            model: projectsModel.recentScores

                            delegate: ProjectCard {
                                required property var modelData
                                required property int index

                                width: prv.recentCardWidth
                                item: modelData
                                compact: true

                                navigation.panel: recentNavPanel
                                navigation.column: index

                                onOpenRequested: projectsModel.openScore(modelData.scorePath)
                            }
                        }
                    }
                }
            }

            // --- Projects -------------------------------------------------------------
            Column {
                width: parent.width
                spacing: 14

                RowLayout {
                    width: parent.width
                    spacing: 10

                    StyledTextLabel {
                        Layout.fillWidth: true
                        text: qsTrc("project", "Projects")
                        font: ui.theme.headerBoldFont
                        horizontalAlignment: Text.AlignLeft
                    }

                    FlatButton {
                        text: qsTrc("project", "Open projects folder")
                        icon: IconCode.OPEN_FILE
                        orientation: Qt.Horizontal

                        navigation.name: "OpenProjectsFolder"
                        navigation.panel: topNavPanel
                        navigation.column: 4

                        onClicked: projectsModel.openProjectsFolder()
                    }
                }

                Rectangle {
                    width: parent.width
                    height: Math.max(categoriesColumn.implicitHeight, projectsColumn.implicitHeight) + 2
                    radius: 12
                    color: ui.theme.backgroundPrimaryColor
                    border.color: ui.theme.strokeColor
                    border.width: 1

                    Row {
                        x: 1
                        y: 1
                        width: parent.width - 2

                        CategoriesList {
                            id: categoriesColumn

                            width: 260
                            pageModel: projectsModel
                            navigationSection: navSec
                            revealTitle: prv.revealTitle
                        }

                        SeparatorLine {
                            orientation: Qt.Vertical
                            height: parent.height
                        }

                        Column {
                            id: projectsColumn

                            width: parent.width - categoriesColumn.width - 1
                            padding: 22
                            spacing: 18

                            readonly property real innerWidth: width - 2 * padding

                            RowLayout {
                                width: projectsColumn.innerWidth
                                spacing: 10

                                StyledTextLabel {
                                    text: projectsModel.currentCategory
                                    font: ui.theme.largeBodyBoldFont
                                }

                                StyledTextLabel {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignLeft
                                    opacity: 0.7
                                    text: projectsModel.activeTags.length > 0
                                          ? qsTrc("project", "%1 of %2 projects").arg(projectsModel.projects.length).arg(projectsModel.projectsInCategoryCount)
                                          : qsTrc("project", "%n project(s)", "", projectsModel.projectsInCategoryCount)
                                }
                            }

                            TagFilterBar {
                                width: projectsColumn.innerWidth
                                pageModel: projectsModel
                                navigationSection: navSec
                            }

                            // Empty category
                            StyledTextLabel {
                                width: projectsColumn.innerWidth
                                visible: projectsModel.projectsInCategoryCount === 0 && projectsModel.currentCategory !== ""
                                topPadding: 30
                                bottomPadding: 30
                                text: qsTrc("project", "No projects in this category yet.\nAdd one from a project’s ⋯ menu → Categories and tags.")
                                opacity: 0.7
                                wrapMode: Text.Wrap
                            }

                            // Filters match nothing
                            Column {
                                width: projectsColumn.innerWidth
                                visible: projectsModel.projectsInCategoryCount > 0 && projectsModel.projects.length === 0
                                topPadding: 24
                                bottomPadding: 24
                                spacing: 12

                                StyledTextLabel {
                                    width: parent.width
                                    text: projectsModel.activeTags.length > 0
                                          ? qsTrc("project", "No scores in %1 match these tags").arg(projectsModel.currentCategory)
                                          : qsTrc("project", "No projects match your search")
                                    font: ui.theme.largeBodyBoldFont
                                    wrapMode: Text.Wrap
                                }

                                StyledTextLabel {
                                    width: parent.width
                                    visible: projectsModel.activeTags.length > 0
                                    text: qsTrc("project", "Remove a tag, switch to “Any selected tag”, or clear the filters to see every score in this category.")
                                    opacity: 0.7
                                    wrapMode: Text.Wrap
                                }

                                FlatButton {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    visible: projectsModel.activeTags.length > 0
                                    text: qsTrc("project", "Clear filters")
                                    accentButton: true

                                    navigation.name: "ClearFiltersEmpty"
                                    navigation.panel: topNavPanel
                                    navigation.column: 5

                                    onClicked: projectsModel.clearTags()
                                }
                            }

                            NavigationPanel {
                                id: cardsNavPanel
                                name: "ProjectCards"
                                section: navSec
                                order: 6
                                direction: NavigationPanel.Both
                                accessible.name: qsTrc("project", "Projects")
                            }

                            Flow {
                                id: cardsFlow

                                width: projectsColumn.innerWidth
                                spacing: prv.cardSpacing

                                readonly property int columns: Math.max(1, Math.floor((width + spacing) / (prv.cardMinWidth + spacing)))
                                readonly property real cardWidth: Math.floor((width - (columns - 1) * spacing) / columns)

                                Repeater {
                                    model: projectsModel.projects

                                    delegate: ProjectCard {
                                        required property var modelData
                                        required property int index

                                        width: cardsFlow.cardWidth
                                        item: modelData
                                        showMenu: true
                                        revealTitle: prv.revealTitle

                                        navigation.panel: cardsNavPanel
                                        navigation.row: Math.floor(index / cardsFlow.columns)
                                        navigation.column: index % cardsFlow.columns

                                        onOpenRequested: projectsModel.openScore(modelData.scorePath)
                                        onMenuItemTriggered: function(itemId) {
                                            switch (itemId) {
                                            case "open": projectsModel.openScore(modelData.scorePath); break
                                            case "rename": projectsModel.renameProjectInteractive(modelData.folder); break
                                            case "move": projectsModel.moveProjectInteractive(modelData.folder); break
                                            case "categories-tags": projectsModel.editCategoriesAndTags(modelData.folder); break
                                            case "card-image": projectsModel.editCardImage(modelData.folder); break
                                            case "remove-card-image": projectsModel.removeCardImage(modelData.folder); break
                                            case "pictures": projectsModel.openPicturesFolder(modelData.folder); break
                                            case "reveal": projectsModel.revealInFileBrowser(modelData.folder); break
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
