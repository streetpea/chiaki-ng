import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaki4deck

import "controls" as C

DialogView {
    id: dialog
    property var callback: null
    property bool login
    property var psnurl: ""
    title: qsTr("PSN Login")
    buttonVisible: false
    buttonText: qsTr("✓ Get Account ID")
    buttonEnabled: url.text.trim()
    onAccepted: Chiaki.handlePsnLoginRedirect(url.text.trim())

    StackView.onActivated: {
        if(login)
        {
            loginForm.visible = true
            psnurl = Chiaki.openPsnLink()
            dialog.buttonVisible = true
        }
        else
            accountForm.visible = true
    }

   Item {
        GridLayout {
            id: loginForm
            anchors {
                top: parent.top
                horizontalCenter: parent.horizontalCenter
                topMargin: 50
            }
            visible: false
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

        ColumnLayout {
            id: accountForm
            anchors {
                top: parent.top
                horizontalCenter: parent.horizontalCenter
                topMargin: parent.height / 10
            }
            visible: false

            Label {
                id: formLabel
                Layout.alignment: Qt.AlignCenter
                Layout.bottomMargin: 50
                text: qsTr("This requires your PSN privacy settings to allow anyone to find you in your search")
            }

            C.TextField {
                id: usernameField
                Layout.preferredWidth: 400
                Layout.alignment: Qt.AlignCenter
                firstInFocusChain: true
                placeholderText: qsTr("Username")
                onAccepted: submitButton.clicked()
            }

            C.Button {
                id: submitButton
                Layout.preferredWidth: 400
                Layout.alignment: Qt.AlignCenter
                lastInFocusChain: true
                text: qsTr("Submit")
                onClicked: {
                    const username = usernameField.text.trim();
                    if (!username.length || !enabled)
                        return;
                    const request = new XMLHttpRequest();
                    request.onreadystatechange = function() {
                        if (request.readyState === XMLHttpRequest.DONE) {
                            const response = JSON.parse(request.response);
                            const accountId = response["encoded_id"];
                            if (accountId) {
                                dialog.callback(accountId);
                                dialog.close();
                            } else {
                                enabled = true;
                                formLabel.text = qsTr("Error: %1!").arg(response["error"]);
                            }
                        }
                    }
                    request.open("GET", "https://psn.flipscreen.games/search.php?username=%1".arg(encodeURIComponent(username)));
                    request.send();
                    enabled = false;
                }
            }
        }

        Rectangle {
            id: overlay
            anchors.fill: loginForm
            color: "grey"
            opacity: 0.0
            visible: opacity

            MouseArea {
                anchors.fill: parent
            }

            BusyIndicator {
                anchors.centerIn: parent
                layer.enabled: true
                width: 80
                height: width
            }
        }

        Connections {
            target: Chiaki

            function onPsnLoginAccountIdDone(accountId) {
                dialog.callback(accountId);
                dialog.close();
            }
        }
    }
}
