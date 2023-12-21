import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: dialog
    property alias title: titleLabel.text
    property alias buttonText: okButton.text
    property alias buttonEnabled: okButton.enabled
    property alias buttonVisible: okButton.visible
    default property Item mainItem: null

    signal accepted()
    signal rejected()

    function close() {
        stack.pop();
    }

    onMainItemChanged: {
        if (mainItem) {
            mainItem.parent = contentItem;
            mainItem.anchors.fill = contentItem;
        }
    }

    ToolBar {
        id: toolBar
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        height: 80

        RowLayout {
            anchors {
                fill: parent
                leftMargin: 10
                rightMargin: 10
            }

            Button {
                Layout.fillHeight: true
                Layout.preferredWidth: 100
                flat: true
                text: "❮"
                Material.roundedScale: Material.SmallScale
                onClicked: {
                    dialog.rejected();
                    dialog.close();
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                id: okButton
                Layout.fillHeight: true
                flat: true
                padding: 30
                font.pixelSize: 25
                Material.roundedScale: Material.SmallScale
                onClicked: dialog.accepted()
            }
        }

        Label {
            id: titleLabel
            anchors.centerIn: parent
            horizontalAlignment: Qt.AlignHCenter
            verticalAlignment: Qt.AlignVCenter
            font.bold: true
            font.pixelSize: 26
        }
    }

    Item {
        id: contentItem
        anchors {
            top: toolBar.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
    }
}
