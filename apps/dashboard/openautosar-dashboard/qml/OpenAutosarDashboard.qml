// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1280
    height: 720
    visible: true
    title: "openAUTOSAR Engineering Dashboard"
    color: "#F4F5F7"

    property string snapshotPath: "/var/lib/openautosar/dashboard-snapshot.json"
    property string serviceState: "Degraded"
    property string lastDistance: "1425 mm"
    property string serviceEvents: "4"
    property string phmState: "Degraded"
    property string phmStatus: "ServiceUnavailable"
    property string phmAction: "EnterDegradedMode"
    property string diagnosticSession: "Default"
    property string dtcCount: "2"
    property string updateState: "Idle"

    function reloadSnapshot() {
        var request = new XMLHttpRequest()
        request.open("GET", "file://" + snapshotPath)
        request.onreadystatechange = function() {
            if (request.readyState !== XMLHttpRequest.DONE || request.status !== 0) {
                return
            }

            var data = JSON.parse(request.responseText)
            serviceState = data.ultrasonic.state
            lastDistance = data.ultrasonic.last_valid_distance_mm === null
                ? "none"
                : data.ultrasonic.last_valid_distance_mm + " mm"
            serviceEvents = data.ultrasonic.service_events
            phmState = data.health.state
            phmStatus = data.health.status
            phmAction = data.health.action
            diagnosticSession = data.diagnostics.session
            dtcCount = data.diagnostics.dtc_count
            updateState = data.update.state

            dtcModel.clear()
            for (var dtcIndex = 0; dtcIndex < data.diagnostics.dtcs.length; ++dtcIndex) {
                dtcModel.append(data.diagnostics.dtcs[dtcIndex])
            }

            faultModel.clear()
            for (var faultIndex = 0; faultIndex < data.fault_history.length; ++faultIndex) {
                faultModel.append(data.fault_history[faultIndex])
            }
        }
        request.send()
    }

    Component.onCompleted: reloadSnapshot()

    ListModel {
        id: dtcModel
        ListElement {
            code: "0x0A5001"
            status: 9
            origin: "oa-ultrasonic-gateway-smoke"
            description: "ultrasonic CRC mismatch"
        }
        ListElement {
            code: "0x0A5003"
            status: 137
            origin: "oa-ultrasonic-gateway-smoke"
            description: "ultrasonic alive counter jump"
        }
    }

    ListModel {
        id: faultModel
        ListElement {
            sample_index: 3
            fault: "CrcMismatch"
            dtc: "0x0A5001"
            service_state: "Unavailable"
            degraded_requested: false
        }
        ListElement {
            sample_index: 4
            fault: "AliveCounterJump"
            dtc: "0x0A5003"
            service_state: "Degraded"
            degraded_requested: true
        }
    }

    header: ToolBar {
        background: Rectangle { color: "#FFFFFF" }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            spacing: 12

            Label {
                text: "openAUTOSAR Engineering Dashboard"
                font.pixelSize: 20
                font.weight: Font.DemiBold
                color: "#1D232A"
                Layout.fillWidth: true
            }

            Label {
                text: "qemux86-64"
                font.pixelSize: 13
                color: "#52616F"
            }

            Button {
                text: "Reload"
                onClicked: reloadSnapshot()
            }
        }
    }

    GridLayout {
        anchors.fill: parent
        anchors.margins: 18
        columns: 4
        rowSpacing: 14
        columnSpacing: 14

        StatusTile {
            title: "Ultrasonic Service"
            primary: serviceState
            detail: "distance " + lastDistance + "  events " + serviceEvents
            accent: serviceState === "Available" ? "#177245" : "#B45309"
            Layout.fillWidth: true
        }

        StatusTile {
            title: "Platform Health"
            primary: phmState
            detail: phmStatus + "  " + phmAction
            accent: phmState === "Healthy" ? "#177245" : "#B42318"
            Layout.fillWidth: true
        }

        StatusTile {
            title: "Diagnostics"
            primary: dtcCount + " DTCs"
            detail: "session " + diagnosticSession
            accent: dtcCount === "0" ? "#177245" : "#C2410C"
            Layout.fillWidth: true
        }

        StatusTile {
            title: "Update"
            primary: updateState
            detail: "slot A active  slot B inactive"
            accent: "#1F7A8C"
            Layout.fillWidth: true
        }

        Frame {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 0

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                Label {
                    text: "Diagnostic Trouble Codes"
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    color: "#1D232A"
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: dtcModel
                    delegate: DtcRow {
                        width: ListView.view.width
                        codeText: code
                        statusText: status
                        originText: origin
                        descriptionText: description
                    }
                }
            }
        }

        Frame {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 0

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                Label {
                    text: "Fault History"
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    color: "#1D232A"
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: faultModel
                    delegate: FaultRow {
                        width: ListView.view.width
                        sampleText: sample_index
                        faultText: fault
                        dtcText: dtc
                        stateText: service_state
                        degradedText: degraded_requested ? "yes" : "no"
                    }
                }
            }
        }
    }
}
