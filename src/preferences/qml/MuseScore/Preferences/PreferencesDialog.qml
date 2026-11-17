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
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Preferences

StyledDialogView {
    id: root

    title: qsTrc("preferences", "Preferences")

    contentWidth: 880
    contentHeight: 640
    resizable: true

    property string currentPageId: ""
    property var params: null

    PreferencesContent {
        anchors.fill: parent

        navigationSection: root.navigationSection
        currentPageId: root.currentPageId
        params: root.params

        onHideRequested: root.hide()
        onRejectRequested: root.reject()
    }
}
