import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaki4deck

import "controls" as C

DialogView {
    property var psnurl
    property var expired
    title: {
        if(expired)
            qsTr("Credentials Expired: Refresh PSN Remote Connection")
        else
            qsTr("Setup Automatic PSN Remote Connection")
    }
    buttonText: qsTr("✓ Setup")
    buttonEnabled: url.text.trim()
    onAccepted: {
        logDialog.open()
        Chiaki.initPsnAuth(url.text.trim(), function(msg, ok, done) {
            if (!done)
                logArea.text += msg + "\n";
            else
            {
                logArea.text += msg + "\n";
                logDialog.standardButtons = Dialog.Close;
            }
        });
    }

    Item {
        GridLayout {
            anchors {
                top: parent.top
                horizontalCenter: parent.horizontalCenter
                topMargin: 50
            }
            columns: 2
            rowSpacing: 10
            columnSpacing: 20

            Label {
                text: qsTr("Open Web Browser with following link")
                visible: psnurl
            }

            TextField {
                id: openurl
                text: psnurl
                visible: psnurl
                KeyNavigation.right: copyUrl
                Layout.preferredWidth: 400
                C.Button {
                    id: copyUrl
                    text: qsTr("Click to Copy URL")
                    anchors {
                        left: parent.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: 10
                    }
                    onClicked: {
                        openurl.selectAll()
                        openurl.copy()
                    }
                    KeyNavigation.up: openurl
                    KeyNavigation.left: openurl
                    KeyNavigation.down: url
                    KeyNavigation.right: url
                }
            }

            Label {
                text: qsTr("Redirect URL from Web Browser")
            }

            TextField {
                id: url
                Layout.preferredWidth: 400
                KeyNavigation.right: pasteUrl
                KeyNavigation.left: copyUrl
                KeyNavigation.up: copyUrl
                KeyNavigation.down: pasteUrl
                C.Button {
                    id: pasteUrl
                    text: qsTr("Click to Paste URL")
                    anchors {
                        left: parent.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: 10
                    }
                    onClicked: url.paste()
                    KeyNavigation.left: url
                    lastInFocusChain: true
                }
            }
        }

        Dialog {
            id: logDialog
            parent: Overlay.overlay
            x: Math.round((root.width - width) / 2)
            y: Math.round((root.height - height) / 2)
            title: qsTr("Create PSN Automatic Remote Connection Token")
            modal: true
            closePolicy: Popup.NoAutoClose
            standardButtons: Dialog.Cancel
            Material.roundedScale: Material.MediumScale
            onOpened: logArea.forceActiveFocus()
            onClosed: root.showMainView()

            Flickable {
                id: logFlick
                implicitWidth: 600
                implicitHeight: 400
                clip: true
                contentWidth: logArea.contentWidth
                contentHeight: logArea.contentHeight
                flickableDirection: Flickable.AutoFlickIfNeeded
                ScrollIndicator.vertical: ScrollIndicator { }

                Label {
                    id: logArea
                    width: logFlick.width
                    wrapMode: TextEdit.Wrap
                    Keys.onReturnPressed: if (logDialog.standardButtons == Dialog.Close) logDialog.close()
                    Keys.onEscapePressed: logDialog.close()
                }
            }
        }
    }
}