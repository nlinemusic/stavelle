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

import Muse.Ui

//! Jubelle: shows a picture in a square (or any box) exactly as on a project card:
//! scaled to cover the box without distortion, positioned by posX / posY (0–100 %).
Item {
    id: root

    property alias source: image.source
    property real posX: 50
    property real posY: 50
    property bool showGuides: false

    readonly property real scaleFactor: (image.implicitWidth > 0 && image.implicitHeight > 0)
                                        ? Math.max(width / image.implicitWidth, height / image.implicitHeight) : 1
    readonly property real overflowX: Math.max(0, image.implicitWidth * scaleFactor - width)
    readonly property real overflowY: Math.max(0, image.implicitHeight * scaleFactor - height)

    clip: true

    Rectangle {
        anchors.fill: parent
        color: "#1A1A23"
    }

    Image {
        id: image

        width: implicitWidth * root.scaleFactor
        height: implicitHeight * root.scaleFactor
        x: -root.overflowX * root.posX / 100
        y: -root.overflowY * root.posY / 100

        asynchronous: true
        cache: false
        smooth: true
        mipmap: true
    }

    Item {
        anchors.fill: parent
        visible: root.showGuides

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: "#E600A9"
            border.width: 2
        }

        Repeater {
            model: 2
            Rectangle {
                required property int index
                x: root.width * (index + 1) / 3
                width: 1
                height: root.height
                color: "#40FFFFFF"
            }
        }

        Repeater {
            model: 2
            Rectangle {
                required property int index
                y: root.height * (index + 1) / 3
                height: 1
                width: root.width
                color: "#40FFFFFF"
            }
        }
    }
}
