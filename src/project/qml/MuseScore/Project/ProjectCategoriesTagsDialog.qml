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

//! Jubelle: choose every category a score is listed in, and its tags.
//! The score is never copied; extra categories and tags are stored outside the score file.
StyledDialogView {
    id: root

    property string projectName: ""
    property var categories: []
    property var checkedCategories: []
    property string homeCategory: ""
    property var tags: []
    property var allTags: []

    objectName: "ProjectCategoriesTagsDialog"

    contentWidth: 600
    contentHeight: content.implicitHeight
    margins: 24

    modal: true

    property var selected: []
    property var currentTags: []
    property string draftTag: ""

    Component.onCompleted: {
        root.selected = root.checkedCategories.slice()
        root.currentTags = root.tags.slice()
    }

    function isChecked(category) {
        return root.selected.indexOf(category) >= 0
    }

    function toggleCategory(category) {
        var list = root.selected.slice()
        var i = list.indexOf(category)
        if (i >= 0) {
            if (list.length === 1) {
                return // a score needs at least one category
            }
            list.splice(i, 1)
        } else {
            list.push(category)
        }
        root.selected = list
    }

    function hasTag(tag) {
        var t = tag.toLowerCase()
        for (var i = 0; i < root.currentTags.length; ++i) {
            if (root.currentTags[i].toLowerCase() === t) {
                return true
            }
        }
        return false
    }

    function addTag(tag) {
        var t = tag.trim()
        if (t === "" || hasTag(t)) {
            return
        }
        for (var i = 0; i < root.allTags.length; ++i) {
            if (root.allTags[i].toLowerCase() === t.toLowerCase()) {
                t = root.allTags[i] // reuse the existing spelling
                break
            }
        }
        var list = root.currentTags.slice()
        list.push(t)
        root.currentTags = list
    }

    function removeTag(tag) {
        var list = root.currentTags.slice()
        var i = list.indexOf(tag)
        if (i >= 0) {
            list.splice(i, 1)
        }
        root.currentTags = list
    }

    readonly property string newHome: isChecked(root.homeCategory) ? root.homeCategory : (root.selected.length > 0 ? root.categories.filter(function(c) { return root.isChecked(c) })[0] : "")

    function done() {
        root.ret = { errcode: 0, value: { "categories": root.selected, "tags": root.currentTags } }
        root.hide()
    }

    Column {
        id: content

        width: parent.width
        spacing: 16

        StyledTextLabel {
            width: parent.width
            text: qsTrc("project", "Categories and tags for “%1”").arg(root.projectName)
            font: ui.theme.largeBodyBoldFont
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
        }

        StyledTextLabel {
            width: parent.width
            text: qsTrc("project", "Tick every category this score belongs to. The score is not copied: it is simply listed in each one.")
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            opacity: 0.8
        }

        NavigationPanel {
            id: catNavPanel
            name: "CategoriesList"
            section: root.navigationSection
            direction: NavigationPanel.Vertical
            order: 1
        }

        Column {
            width: parent.width
            spacing: 6

            Repeater {
                model: root.categories

                delegate: RowLayout {
                    id: catRow

                    required property var modelData
                    required property int index

                    width: parent.width
                    spacing: 12

                    CheckBox {
                        Layout.fillWidth: true

                        text: catRow.modelData
                        checked: root.isChecked(catRow.modelData)

                        navigation.name: catRow.modelData
                        navigation.panel: catNavPanel
                        navigation.row: catRow.index

                        onClicked: root.toggleCategory(catRow.modelData)
                    }

                    StyledTextLabel {
                        visible: catRow.modelData === root.newHome
                        text: qsTrc("project", "Project folder is stored here")
                        opacity: 0.7
                    }
                }
            }
        }

        StyledTextLabel {
            width: parent.width
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            opacity: 0.8
            text: {
                if (root.selected.length === 1) {
                    return qsTrc("project", "A score needs at least one category, so the last one can’t be unticked. Tick another category first.")
                }
                if (!root.isChecked(root.homeCategory)) {
                    return qsTrc("project", "The project folder will move from “%1” to “%2”, safely, with its score, backups, exports and card image.")
                    .arg(root.homeCategory).arg(root.newHome)
                }
                return qsTrc("project", "Unticking a category only removes the score from that list. The score itself is never deleted.")
            }
        }

        SeparatorLine {}

        StyledTextLabel {
            text: qsTrc("project", "Tags")
            font: ui.theme.bodyBoldFont
        }

        NavigationPanel {
            id: tagNavPanel
            name: "TagsPanel"
            section: root.navigationSection
            direction: NavigationPanel.Horizontal
            order: 2
        }

        Flow {
            width: parent.width
            spacing: 8

            Repeater {
                model: root.currentTags

                delegate: FlatButton {
                    required property var modelData
                    required property int index

                    text: modelData + "  ×"
                    accentButton: true
                    toolTipTitle: qsTrc("project", "Remove tag")

                    navigation.name: "RemoveTag " + modelData
                    navigation.panel: tagNavPanel
                    navigation.column: index

                    onClicked: root.removeTag(modelData)
                }
            }
        }

        RowLayout {
            width: parent.width
            spacing: 8

            TextInputField {
                id: tagField

                Layout.fillWidth: true
                hint: qsTrc("project", "Type a new tag or pick one below")
                currentText: root.draftTag

                navigation.panel: tagNavPanel
                navigation.column: 1000

                onTextChanged: function(newTextValue) {
                    root.draftTag = newTextValue
                }

                onAccepted: {
                    root.addTag(root.draftTag)
                    root.draftTag = ""
                }
            }

            FlatButton {
                text: qsTrc("project", "Add tag")
                enabled: root.draftTag.trim() !== ""

                navigation.panel: tagNavPanel
                navigation.column: 1001

                onClicked: {
                    root.addTag(root.draftTag)
                    root.draftTag = ""
                }
            }
        }

        Flow {
            width: parent.width
            spacing: 8

            StyledTextLabel {
                height: 28
                verticalAlignment: Text.AlignVCenter
                text: qsTrc("project", "Tags you already use:")
                visible: suggestions.count > 0
                opacity: 0.8
            }

            Repeater {
                id: suggestions

                model: root.allTags.filter(function(t) { return !root.hasTag(t) })

                delegate: FlatButton {
                    required property var modelData
                    required property int index

                    text: "+ " + modelData

                    navigation.name: "SuggestTag " + modelData
                    navigation.panel: tagNavPanel
                    navigation.column: 2000 + index

                    onClicked: root.addTag(modelData)
                }
            }
        }

        StyledTextLabel {
            width: parent.width
            text: qsTrc("project", "Categories and tags are kept in Jubelle’s own project information, never inside the score file. Changing them never changes the score.")
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            opacity: 0.7
        }

        ButtonBox {
            width: parent.width

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 3

            FlatButton {
                text: qsTrc("global", "Cancel")
                buttonRole: ButtonBoxModel.RejectRole
                buttonId: ButtonBoxModel.Cancel

                onClicked: root.reject()
            }

            FlatButton {
                text: qsTrc("global", "Save")
                accentButton: true
                enabled: root.selected.length > 0
                buttonRole: ButtonBoxModel.AcceptRole
                buttonId: ButtonBoxModel.Ok

                onClicked: root.done()
            }
        }
    }
}
