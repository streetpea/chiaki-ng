import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.streetpea.chiaki4deck

DialogView {
    title: qsTr("Add Manual Console")
    buttonText: qsTr("✓ Save")
    buttonEnabled: hostField.text.trim()
    onAccepted: {
        Chiaki.addManualHost(consoleCombo.model[consoleCombo.currentIndex].index, hostField.text);
        close();
    }

    Item {
        GridLayout {
            anchors {
                top: parent.top
                horizontalCenter: parent.horizontalCenter
                topMargin: 20
            }
            columns: 2
            rowSpacing: 20
            columnSpacing: 20

            Label {
                text: qsTr("Host:")
            }

            TextField {
                id: hostField
                Layout.preferredWidth: 400
            }

            Label {
                text: qsTr("Registered Consoles:")
            }

            ComboBox {
                id: consoleCombo
                Layout.preferredWidth: 400
                textRole: "name"
                model: {
                    let m = [];
                    m.push({
                        name: qsTr("Register on first Connection"),
                        index: -1,
                    });
                    for (let i = 0; i < Chiaki.hosts.length; ++i) {
                        let host = Chiaki.hosts[i];
                        if (!host.registered)
                            continue;
                        m.push({
                            name: "%1 (%2)".arg(host.mac).arg(host.name),
                            index: i,
                        });
                    }
                    return m;
                }
            }
        }
    }
}
