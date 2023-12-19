import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

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
    onOpened: label.forceActiveFocus()
    onAccepted: {
        restoreFocus();
        callback();
    }
    onRejected: restoreFocus()

    function restoreFocus() {
        if (restoreFocusItem)
            restoreFocusItem.forceActiveFocus(Qt.TabFocus);
    }

    ColumnLayout {
        spacing: 20

        Label {
            id: label
            Keys.onReturnPressed: dialog.accept()
            Keys.onEscapePressed: dialog.reject()
        }

        RowLayout {
            Layout.alignment: Qt.AlignCenter
            spacing: 20

            Button {
                focusPolicy: Qt.NoFocus
                text: qsTr("Yes")
                flat: true
                onClicked: dialog.accept()
                Material.roundedScale: Material.SmallScale
            }

            Button {
                focusPolicy: Qt.NoFocus
                text: qsTr("No")
                flat: true
                onClicked: dialog.reject()
                Material.roundedScale: Material.SmallScale
            }
        }
    }
}
