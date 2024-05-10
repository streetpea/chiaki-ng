import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaki4deck

Rectangle {
    id: view
    property bool allowClose: false
    color: "black"

    function stop() {
        if (!allowClose)
            return;
        Chiaki.stopAutoConnect();
        root.showMainView();
    }

    Keys.onReturnPressed: view.stop()
    Keys.onEscapePressed: view.stop()

    Shortcut {
        sequence: "Ctrl+Q"
        onActivated: view.stop()
    }

    MouseArea {
        anchors.fill: parent
        enabled: view.allowClose
        onClicked: view.stop()
    }

    Label {
        id: infoLabel
        anchors.centerIn: parent
        opacity: view.allowClose ? 1.0 : 0.0
        visible: opacity
        text: qsTr("Waiting for console...")

        Behavior on opacity { NumberAnimation { duration: 250 } }
    }

    Item {
        anchors {
            top: parent.verticalCenter
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }

        BusyIndicator {
            id: spinner
            anchors.centerIn: parent
            width: 70
            height: width
        }
    }

    Timer {
        interval: 1500
        running: true
        onTriggered: view.allowClose = true
    }
}
