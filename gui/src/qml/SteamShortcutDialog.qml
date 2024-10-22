import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

import "controls" as C

DialogView {
    property bool opening: false
    title: qsTr("Create Non-Steam Game")
    buttonText: qsTr("✓ Create")
    buttonEnabled: name.text.trim() && !opening
    onAccepted: {
        opening = true;
        logDialog.open();
        Chiaki.createSteamShortcut(name.text.trim(), options.text.trim(), function(msg, ok, done) {
            if (!done)
                logArea.text += msg + "\n";
            else
            {
                logArea.text += msg + "\n";
                logDialog.standardButtons = Dialog.Close;
                opening = false;
            }
        });
    }

    Item {
        GridLayout {
            anchors {
                top: parent.top
                horizontalCenter: parent.horizontalCenter
                topMargin: 20
            }
            columns: 2
            rowSpacing: 10
            columnSpacing: 20

            Label {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Steam Game Name")
            }

            C.TextField {
                id: name
                Layout.preferredWidth: 400
                text: Chiaki.settings.currentProfile ? qsTr("chiaki-ng ") + Chiaki.settings.currentProfile: qsTr("chiaki-ng")
                firstInFocusChain: true
            }

            Label {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Launch Options [Optional]")
            }

            C.TextField {
                id: options
                text: Chiaki.settings.currentProfile ? qsTr("--profile=") + Chiaki.settings.currentProfile : ""
                Layout.preferredWidth: 400
            }
        }

        Dialog {
            id: logDialog
            parent: Overlay.overlay
            x: Math.round((root.width - width) / 2)
            y: Math.round((root.height - height) / 2)
            title: qsTr("Create non-Steam game")
            modal: true
            closePolicy: Popup.NoAutoClose
            standardButtons: Dialog.Cancel
            Material.roundedScale: Material.MediumScale
            onOpened: logArea.forceActiveFocus()
            onClosed: restartDialog.open()
            KeyNavigation.up: {
                if(logScrollbar.position > 0.001)
                    logFlick.flick(0, 500);
            }
            KeyNavigation.down: {
                if(logScrollbar.position < 1.0 - logScrollbar.size - 0.001)
                    logFlick.flick(0, -500); 
            }

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

        Dialog {
            id: restartDialog
            parent: Overlay.overlay
            x: Math.round((root.width - width) / 2)
            y: Math.round((root.height - height) / 2)
            title: qsTr("Please Restart Steam")
            modal: true
            closePolicy: Popup.NoAutoClose
            standardButtons: Dialog.Close
            Material.roundedScale: Material.MediumScale
            onOpened: restartArea.forceActiveFocus()
            onClosed: stack.pop()

            Flickable {
                id: restartFlick
                implicitWidth: 600
                implicitHeight: 25
                clip: true
                contentWidth: restartArea.contentWidth
                contentHeight: restartArea.contentHeight
                flickableDirection: Flickable.AutoFlickIfNeeded
                ScrollIndicator.vertical: ScrollIndicator { }

                Label {
                    id: restartArea
                    width: restartFlick.width
                    text: "In order for " + name.text.trim() + " to appear in Steam, Steam must be restarted!"
                    wrapMode: TextEdit.Wrap
                    Keys.onReturnPressed: if (restartDialog.standardButtons == Dialog.Close) restartDialog.close()
                    Keys.onEscapePressed: restartDialog.close()
                }
            }
        }
    }
}