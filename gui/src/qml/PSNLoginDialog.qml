import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

import "controls" as C

DialogView {
    id: dialog
    property var callback: null
    property bool login
    property bool submitting: false
    property var psnurl: ""
    title: qsTr("PSN Login")
    buttonVisible: false
    buttonText: qsTr("✓ Get Account ID")
    buttonEnabled: !submitting && url.text.trim()
    onAccepted: {
        submitting = true;
        Chiaki.handlePsnLoginRedirect(url.text.trim());
    }

    StackView.onActivated: {
        if(login)
        {
            loginForm.visible = true
            dialog.buttonVisible = true
            psnurl = Chiaki.openPsnLink()
            if(psnurl)
            {
                openurl.selectAll()
                openurl.copy()
            }
            pasteUrl.forceActiveFocus(Qt.TabFocusReason)
        }
        else
        {
            accountForm.visible = true
            usernameField.forceActiveFocus(Qt.TabFocusReason)
            usernameField.readOnly = false;
            Qt.inputMethod.show();
        }
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
                id: errorHeader
                visible: false
                text: "Retrieving PSN account ID failed with error"
            }

            Label {
                id: errorLabel
                visible: false
            }

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
                    KeyNavigation.priority: KeyNavigation.BeforeItem
                    onClicked: url.paste()
                    KeyNavigation.left: url
                    KeyNavigation.up: {
                        if(psnurl)
                            copyUrl
                        else
                            pasteUrl
                    }
                    KeyNavigation.down: pasteUrl
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
                submitting = false;
                dialog.close();
            }

            function onPsnLoginAccountIdError(error) {
                errorHeader.visible = true;
                errorLabel.text = error;
                errorLabel.visible = true;
                submitting = false;
            }
        }
    }
}
