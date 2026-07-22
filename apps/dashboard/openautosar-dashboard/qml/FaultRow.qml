// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    property string sampleText: ""
    property string faultText: ""
    property string dtcText: ""
    property string stateText: ""
    property string degradedText: ""

    height: 56
    color: "transparent"
    border.color: "#E2E6EA"

    GridLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        columns: 5
        columnSpacing: 10

        Label {
            text: sampleText
            font.pixelSize: 13
            color: "#52616F"
            Layout.preferredWidth: 42
        }

        Label {
            text: faultText
            font.pixelSize: 13
            font.weight: Font.DemiBold
            color: "#1D232A"
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        Label {
            text: dtcText
            font.pixelSize: 13
            color: "#B42318"
            Layout.preferredWidth: 90
        }

        Label {
            text: stateText
            font.pixelSize: 13
            color: "#52616F"
            Layout.preferredWidth: 94
        }

        Label {
            text: degradedText
            font.pixelSize: 13
            color: degradedText === "yes" ? "#B45309" : "#177245"
            Layout.preferredWidth: 52
        }
    }
}
