import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaki4deck

Item {
    id: root

    function showMainView() {
        if (stack.depth > 1)
            stack.pop(stack.get(0));
        else
            stack.replace(stack.get(0), mainViewComponent);
    }

    function showStreamView() {
        stack.replace(stack.get(0), streamViewComponent);
    }

    function showManualHostDialog() {
        stack.push(manualHostDialogComponent);
    }

    function showConfirmDialog(title, text, callback) {
        confirmDialog.title = title;
        confirmDialog.text = text;
        confirmDialog.callback = callback;
        confirmDialog.restoreFocusItem = Window.window.activeFocusItem;
        confirmDialog.open();
    }

    Component.onCompleted: {
        if (Chiaki.session)
            stack.replace(stack.get(0), streamViewComponent, {}, StackView.Immediate);
    }

    Pane {
        anchors.fill: parent
        visible: !Chiaki.window.hasVideo || !Chiaki.window.keepVideo
    }

    StackView {
        id: stack
        anchors.fill: parent
        hoverEnabled: false
        initialItem: mainViewComponent

        replaceEnter: Transition {
            PropertyAnimation {
                property: "opacity"
                from: 0.01
                to: 1.0
                duration: 200
            }
        }

        replaceExit: Transition {
            PropertyAnimation {
                property: "opacity"
                from: 1.0
                to: 0.0
                duration: 100
            }
        }
    }

    Rectangle {
        anchors {
            bottom: parent.bottom
            horizontalCenter: parent.horizontalCenter
            bottomMargin: 30
        }
        color: Material.accent
        width: errorLayout.width + 40
        height: errorLayout.height + 20
        radius: 8
        opacity: errorHideTimer.running ? 0.8 : 0.0

        Behavior on opacity { NumberAnimation { duration: 500 } }

        ColumnLayout {
            id: errorLayout
            anchors.centerIn: parent

            Label {
                id: errorTitleLabel
                Layout.alignment: Qt.AlignCenter
                font.bold: true
                font.pixelSize: 24
            }

            Label {
                id: errorTextLabel
                Layout.alignment: Qt.AlignCenter
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 20
            }
        }

        Timer {
            id: errorHideTimer
            interval: 2000
        }
    }

    ConfirmDialog {
        id: confirmDialog
    }

    Connections {
        target: Chiaki

        function onSessionChanged() {
            if (Chiaki.session)
                root.showStreamView();
        }

        function onError(title, text) {
            errorTitleLabel.text = title;
            errorTextLabel.text = text;
            errorHideTimer.start();
        }
    }

    Component {
        id: mainViewComponent
        MainView { }
    }

    Component {
        id: streamViewComponent
        StreamView { }
    }

    Component {
        id: manualHostDialogComponent
        ManualHostDialog { }
    }
}
