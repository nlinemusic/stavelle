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

import Muse.Ui
import Muse.UiComponents

BaseSection {
    id: root

    title: qsTrc("preferences", "Automatic update check")

    property bool isAppUpdatable: true
    property alias needCheckForNewAppVersion: needCheckBox.checked
    property string museScorePrivacyPolicyUrl

    property alias testingReleases: testingBox.checked

    signal needCheckForNewAppVersionChangeRequested(bool check)
    signal testingReleasesChangeRequested(bool include)

    CheckBox {
        id: needCheckBox
        width: parent.width

        text: qsTrc("preferences", "Check for a new version of Jubelle when it starts")

        visible: root.isAppUpdatable

        navigation.name: "NeedCheckBox"
        navigation.panel: root.navigation
        navigation.row: 0

        onClicked: {
            root.needCheckForNewAppVersionChangeRequested(!checked)
        }
    }

    // Jubelle R3: testing releases are GitHub pre-releases; off unless you want them
    CheckBox {
        id: testingBox
        width: parent.width

        text: qsTrc("preferences", "Also offer testing releases")

        visible: root.isAppUpdatable

        navigation.name: "TestingReleasesBox"
        navigation.panel: root.navigation
        navigation.row: 1

        onClicked: {
            root.testingReleasesChangeRequested(!checked)
        }
    }

    StyledTextLabel {
        width: parent.width

        text: qsTrc("preferences", "Jubelle looks only at its own releases on GitHub, and installs an update only if it is signed with Jubelle’s release key and arrived unchanged. Nothing about you or your scores is sent. You can always check by hand with Help › Check for updates.")

        horizontalAlignment: Qt.AlignLeft
        wrapMode: Text.WordWrap
        maximumLineCount: 3
    }
}
