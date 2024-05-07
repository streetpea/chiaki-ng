import QtQuick
import QtQuick.Controls

import org.streetpea.chiaki4deck

Rectangle {
    id: view
    property bool allowClose: false
    property bool cancelling: false
    color: "black"

    function stop() {
        if (!allowClose)
            return;
        cancelling = true;
        Chiaki.psnCancel();
    }

    Keys.onReturnPressed: view.stop()
    Keys.onEscapePressed: view.stop()

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
        text: {
            if(cancelling)
                qsTr("Cancelling connection with console over PSN ...")
            else
                qsTr("Establishing connection with console over PSN ...")
        }

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
        id: closeTimer
        interval: 1500
        running: true
        onTriggered: view.allowClose = true
    }

    Timer {
        id: failTimer
        interval: 1500
        running: false
        onTriggered: root.showMainView()
    }

    Connections {
        target: Chiaki

        function onConnectStateChanged() {
            switch(Chiaki.connectState)
            {
                case Chiaki.PsnConnectState.LinkingConsole:
                    infoLabel.text = qsTr("Linking Chiaki4deck with PlayStation console ...")
                    view.allowClose = false
                    break
                case Chiaki.PsnConnectState.DataConnectionStart:
                    infoLabel.text = qsTr("Console Linked ... Establising data connection with console over PSN ...")
                    view.allowClose = true
                    break
                case Chiaki.PsnConnectState.DataConnectionFinished:
                    root.showStreamView()
                    break
                case Chiaki.PsnConnectState.ConnectFailed:
                    qsTr("Connection over PSN failed closing ...")
                    failTimer.running = true
                    break
            }
        }
    }
}
