// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    property string codeText: ""
    property string statusText: ""
    property string originText: ""
    property string descriptionText: ""

    height: 64
    color: "transparent"
    border.color: "#E2E6EA"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 12

        Label {
            text: codeText
            font.pixelSize: 14
            font.weight: Font.DemiBold
            color: "#1D232A"
            Layout.preferredWidth: 96
        }

        Label {
            text: statusText
            font.pixelSize: 13
            color: "#B42318"
            Layout.preferredWidth: 48
        }

        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true

            Label {
                text: descriptionText
                font.pixelSize: 13
                color: "#1D232A"
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Label {
                text: originText
                font.pixelSize: 12
                color: "#52616F"
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
    }
}
