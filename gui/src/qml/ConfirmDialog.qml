import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import "controls" as C

Dialog {
    id: dialog
    property alias text: label.text
    property var callback
    property Item restoreFocusItem
    parent: Overlay.overlay
    x: Math.round((root.width - width) / 2)
    y: Math.round((root.height - height) / 2)
    modal: true
    Material.roundedScale: Material.MediumScale
    onOpened: yesButton.forceActiveFocus(Qt.TabFocusReason)
    onAccepted: {
        restoreFocus();
        callback();
    }
    onRejected: restoreFocus()
    onClosed: restoreFocus()

    function restoreFocus() {
        if (restoreFocusItem)
            restoreFocusItem.forceActiveFocus(Qt.TabFocusReason);
        yesButton.focus = false;
    }

    Component.onCompleted: {
        header.horizontalAlignment = Text.AlignHCenter;
        // Qt 6.6: Workaround dialog background becoming immediately transparent during close animation
        header.background = null;
    }

    ColumnLayout {
        spacing: 20

        Label {
            id: label
        }

        RowLayout {
            Layout.alignment: Qt.AlignCenter
            spacing: 20

            C.Button {
                id: yesButton
                text: qsTr("Yes")
                Keys.onEscapePressed: dialog.reject()
                flat: true
                leftPadding: 50
                KeyNavigation.priority: KeyNavigation.BeforeItem
                KeyNavigation.right: noButton
                onClicked: dialog.accept()
                Material.roundedScale: Material.SmallScale

                Image {
                    anchors {
                        left: parent.left
                        verticalCenter: parent.verticalCenter
                        leftMargin: 12
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: root.controllerButton("cross")
                }
            }

            C.Button {
                id: noButton
                KeyNavigation.priority: KeyNavigation.BeforeItem
                KeyNavigation.left: yesButton
                Keys.onEscapePressed: dialog.reject()
                text: qsTr("No")
                flat: true
                leftPadding: 50
                onClicked: dialog.reject()
                Material.roundedScale: Material.SmallScale

                Image {
                    anchors {
                        left: parent.left
                        verticalCenter: parent.verticalCenter
                        leftMargin: 12
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: root.controllerButton("moon")
                }
            }
        }
    }
}
