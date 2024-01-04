import QtQuick
import QtWebEngine
import QtQuick.Layouts
import QtQuick.Controls

import org.streetpea.chiaki4deck

DialogView {
    id: dialog
    property var callback: null
    title: qsTr("PSN Login")
    buttonVisible: false

    Item {
        WebEngineView {
            id: webView
            anchors.fill: parent
            url: Chiaki.psnLoginUrl()
            profile: WebEngineProfile { offTheRecord: true }
            onContextMenuRequested: (request) => request.accepted = true
            onNavigationRequested: (request) => {
                if (Chiaki.handlePsnLoginRedirect(request.url)) {
                    request.action = WebEngineNavigationRequest.IgnoreRequest;
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
