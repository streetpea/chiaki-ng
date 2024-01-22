import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.streetpea.chiaki4deck

import "controls" as C

DialogView {
    id: dialog
    property var callback: null
    title: qsTr("PSN Login")
    buttonVisible: false

    Item {
        Item {
            id: webView
            property Item web
            anchors.fill: parent
            Component.onCompleted: {
                try {
                    web = Qt.createQmlObject("
                    import QtWebEngine
                    import org.streetpea.chiaki4deck
                    WebEngineView {
                        signal redirectHandled()
                        profile: WebEngineProfile { offTheRecord: true }
                        onContextMenuRequested: (request) => request.accepted = true
                        onNavigationRequested: (request) => {
                            if (Chiaki.handlePsnLoginRedirect(request.url)) {
                                request.action = WebEngineNavigationRequest.IgnoreRequest;
                                redirectHandled();
                            }
                        }
                    }", webView, "webView");
                    web.url = Chiaki.psnLoginUrl();
                    web.anchors.fill = webView;
                } catch (error) {
                    accountForm.visible = true;
                }
            }

            Connections {
                target: webView.web
                function onRedirectHandled() {
                    overlay.opacity = 0.8;
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
            anchors.fill: webView
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
