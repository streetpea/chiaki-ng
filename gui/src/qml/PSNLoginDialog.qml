import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.streetpea.chiaki4deck

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
                if (!web)
                    return;
                web.url = Chiaki.psnLoginUrl();
                web.anchors.fill = webView;
            }

            Connections {
                target: webView.web
                function onRedirectHandled() {
                    overlay.opacity = 0.8;
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
