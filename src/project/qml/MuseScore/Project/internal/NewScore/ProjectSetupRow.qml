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

//! Jubelle: the "Project" row at the top of the New Score dialog: composition
//! name, category (with "New category…" right here) and where it will be saved.
Rectangle {
    id: root

    property var newScoreModel: null
    property NavigationSection navigationSection: null
    //! The title typed on the second page; used when no composition name is given.
    property string scoreTitle: ""

    property string compositionName: ""
    property string category: ""

    property bool addingCategory: false
    property string newCategoryName: ""
    property string categoryError: ""

    readonly property string effectiveName: {
        var n = root.compositionName.trim()
        if (n === "") {
            n = root.scoreTitle.trim()
        }
        if (n === "") {
            n = qsTrc("project/newscore", "Untitled score")
        }
        return n
    }

    readonly property string mainFolderName: {
        var parts = root.newScoreModel ? String(root.newScoreModel.mainFolder).split(/[\\/]/) : []
        return parts.length > 0 ? parts[parts.length - 1] : ""
    }

    readonly property string nameProblem: root.newScoreModel ? root.newScoreModel.validateCompositionName(root.effectiveName) : ""
    readonly property bool isValid: root.nameProblem === "" && root.category !== ""

    readonly property string folderName: root.newScoreModel && root.isValid
                                         ? root.newScoreModel.projectFolderName(root.category, root.effectiveName) : ""

    height: column.implicitHeight + 20
    radius: 8
    color: ui.theme.backgroundSecondaryColor
    border.color: ui.theme.strokeColor
    border.width: 1

    Component.onCompleted: {
        if (root.newScoreModel) {
            root.category = root.newScoreModel.defaultProjectCategory()
        }
    }

    NavigationPanel {
        id: navPanel
        name: "ProjectSetup"
        section: root.navigationSection
        order: 0
        direction: NavigationPanel.Horizontal
        accessible.name: qsTrc("project/newscore", "Project")
    }

    Column {
        id: column

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 8

        RowLayout {
            width: parent.width
            spacing: 10

            StyledTextLabel {
                text: qsTrc("project/newscore", "Composition name")
                font: ui.theme.bodyBoldFont
            }

            TextInputField {
                id: nameField

                Layout.preferredWidth: 260
                hint: root.scoreTitle.trim() !== "" ? root.scoreTitle.trim()
                                                    : qsTrc("project/newscore", "Uses the title if left empty")
                currentText: root.compositionName

                navigation.name: "CompositionName"
                navigation.panel: navPanel
                navigation.column: 1
                accessible.name: qsTrc("project/newscore", "Composition name")

                onTextChanged: function(newTextValue) {
                    root.compositionName = newTextValue
                }
            }

            StyledTextLabel {
                Layout.leftMargin: 10
                text: qsTrc("project/newscore", "Category")
                font: ui.theme.bodyBoldFont
            }

            StyledDropdown {
                id: categoryDropdown

                Layout.preferredWidth: 220
                visible: !root.addingCategory
                model: root.newScoreModel ? root.newScoreModel.projectCategories : []
                currentIndex: model ? model.indexOf(root.category) : -1

                navigation.name: "ProjectCategory"
                navigation.panel: navPanel
                navigation.column: 2
                navigation.accessible.name: qsTrc("project/newscore", "Category") + ": " + currentText

                onActivated: function(index, value) {
                    root.category = value
                }
            }

            FlatButton {
                visible: !root.addingCategory
                text: qsTrc("project/newscore", "New category…")

                navigation.name: "NewCategory"
                navigation.panel: navPanel
                navigation.column: 3

                onClicked: {
                    root.newCategoryName = ""
                    root.categoryError = ""
                    root.addingCategory = true
                }
            }

            TextInputField {
                id: newCategoryField

                Layout.preferredWidth: 220
                visible: root.addingCategory
                hint: qsTrc("project/newscore", "New category name")
                currentText: root.newCategoryName

                navigation.name: "NewCategoryName"
                navigation.panel: navPanel
                navigation.column: 4

                onTextChanged: function(newTextValue) {
                    root.newCategoryName = newTextValue
                }

                onAccepted: root.createCategory()
                onEscaped: root.addingCategory = false
            }

            FlatButton {
                visible: root.addingCategory
                text: qsTrc("project/newscore", "Create")
                accentButton: true
                enabled: root.newCategoryName.trim() !== ""

                navigation.name: "CreateCategory"
                navigation.panel: navPanel
                navigation.column: 5

                onClicked: root.createCategory()
            }

            FlatButton {
                visible: root.addingCategory
                text: qsTrc("global", "Cancel")

                navigation.name: "CancelNewCategory"
                navigation.panel: navPanel
                navigation.column: 6

                onClicked: root.addingCategory = false
            }

            Item { Layout.fillWidth: true }
        }

        StyledTextLabel {
            width: parent.width
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            opacity: root.isValid && root.categoryError === "" ? 0.75 : 1.0
            color: root.isValid && root.categoryError === "" ? ui.theme.fontPrimaryColor : "#E600A9"
            text: {
                if (root.categoryError !== "") {
                    return root.categoryError
                }
                if (root.nameProblem !== "") {
                    return root.nameProblem
                }
                if (root.category === "") {
                    return qsTrc("project/newscore", "Choose a category.")
                }
                var where = qsTrc("project/newscore", "Will be saved in: %1").arg(root.mainFolderName + " › " + root.category + " › " + root.folderName)
                if (root.folderName !== root.effectiveName) {
                    where += "  " + qsTrc("project/newscore", "(a project named “%1” already exists, so this one gets a number)").arg(root.effectiveName)
                }
                return where
            }
        }
    }

    function createCategory() {
        var name = root.newCategoryName.trim()
        if (name === "" || !root.newScoreModel) {
            return
        }
        var problem = root.newScoreModel.createProjectCategory(name)
        if (problem !== "") {
            root.categoryError = problem
            return
        }
        root.categoryError = ""
        root.category = name
        root.addingCategory = false
    }

    function result() {
        return {
            compositionName: root.effectiveName,
            projectCategory: root.category
        }
    }
}
