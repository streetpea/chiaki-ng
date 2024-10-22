import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

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
    StackView.onActivated: {
        if(psnurl)
        {
            openurl.selectAll()
            openurl.copy()
        }
        pasteUrl.forceActiveFocus(Qt.TabFocusReason)
    }

    Item {
        GridLayout {
            id: linkgrid
            anchors {
                top: parent.top
                horizontalCenter: parent.horizontalCenter
                topMargin: 50
            }
            columns: 2
            rowSpacing: 10
            columnSpacing: 20

            Label {
                text: qsTr("Open Web Browser with copied URL")
                visible: psnurl
            }

            TextField {
                id: openurl
                text: psnurl
                visible: false
                Layout.preferredWidth: 400
            }

            C.Button {
                id: copyUrl
                text: qsTr("Click to Re-Copy URL")
                onClicked: {
                    openurl.selectAll()
                    openurl.copy()
                }
                KeyNavigation.priority: KeyNavigation.BeforeItem
                KeyNavigation.up: copyUrl
                KeyNavigation.down: url
                visible: psnurl
            }

            Label {
                text: qsTr("Redirect URL from Web Browser")
            }

            TextField {
                id: url
                Layout.preferredWidth: 400
                KeyNavigation.priority: {
                    if(readOnly)
                        KeyNavigation.BeforeItem
                    else
                        KeyNavigation.AfterItem
                }
                KeyNavigation.up: {
                    if(psnurl)
                        copyUrl
                    else
                        url
                }
                KeyNavigation.down: url
                KeyNavigation.right: pasteUrl
                C.Button {
                    id: pasteUrl
                    text: qsTr("Click to Paste URL")
                    anchors {
                        left: parent.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: 10
                    }
                    onClicked: url.paste()
                    KeyNavigation.priority: KeyNavigation.BeforeItem
                    KeyNavigation.left: url
                    KeyNavigation.up: {
                        if(psnurl)
                            copyUrl
                        else
                            pasteUrl
                    }
                    KeyNavigation.down: pasteUrl
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
            KeyNavigation.up: {
                if(logScrollbar.position > 0.001)
                    logFlick.flick(0, 500);
            }
            KeyNavigation.down: {
                if(logScrollbar.position < 1.0 - logScrollbar.size - 0.001)
                    logFlick.flick(0, -500);    
            }
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
                ScrollBar.vertical: ScrollBar {
                    id: logScrollbar
                    policy: ScrollBar.AlwaysOn
                }

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