// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    id: tile
    property string title: ""
    property string primary: ""
    property string detail: ""
    property color accent: "#1F7A8C"

    padding: 0
    implicitHeight: 104
    background: Rectangle {
        color: "#FFFFFF"
        border.color: "#D5DAE1"
        radius: 6
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 12

        Rectangle {
            width: 8
            Layout.fillHeight: true
            radius: 3
            color: tile.accent
        }

        ColumnLayout {
            spacing: 6
            Layout.fillWidth: true

            Label {
                text: tile.title
                font.pixelSize: 12
                color: "#52616F"
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Label {
                text: tile.primary
                font.pixelSize: 24
                font.weight: Font.DemiBold
                color: "#1D232A"
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Label {
                text: tile.detail
                font.pixelSize: 12
                color: "#52616F"
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
    }
}
