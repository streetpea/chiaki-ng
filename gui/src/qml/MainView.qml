import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaki4deck

Item {
    StackView.onActivated: forceActiveFocus()
    Keys.onUpPressed: hostsView.decrementCurrentIndex()
    Keys.onDownPressed: hostsView.incrementCurrentIndex()
    Keys.onMenuPressed: settingsButton.clicked()
    Keys.onReturnPressed: if (hostsView.currentItem) hostsView.currentItem.connectToHost()
    Keys.onYesPressed: if (hostsView.currentItem) hostsView.currentItem.wakeUpHost()
    Keys.onNoPressed: if (hostsView.currentItem) hostsView.currentItem.deleteHost()

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
                text: "×"
                font.pixelSize: 60
                focusPolicy: Qt.NoFocus
                onClicked: Qt.quit()
                Material.roundedScale: Material.SmallScale
            }

            Item { Layout.fillWidth: true }

            Button {
                Layout.fillHeight: true
                Layout.preferredWidth: 100
                flat: true
                icon.source: "qrc:/icons/add-24px.svg";
                icon.width: 50
                icon.height: 50
                focusPolicy: Qt.NoFocus
                onClicked: root.showManualHostDialog()
                Material.roundedScale: Material.SmallScale
            }

            Button {
                id: settingsButton
                Layout.fillHeight: true
                Layout.preferredWidth: 100
                flat: true
                icon.source: "qrc:/icons/settings-20px.svg";
                icon.width: 50
                icon.height: 50
                focusPolicy: Qt.NoFocus
                onClicked: root.showSettingsDialog()
                Material.roundedScale: Material.SmallScale
            }
        }

        Label {
            anchors.centerIn: parent
            horizontalAlignment: Qt.AlignHCenter
            verticalAlignment: Qt.AlignVCenter
            textFormat: Text.RichText
            text: "%1 &nbsp;<small>%2</small>".arg(Qt.application.name).arg(Qt.application.version)
            font.bold: true
            font.pixelSize: 26
        }
    }

    ListView {
        id: hostsView
        anchors {
            top: toolBar.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        clip: true
        model: Chiaki.hosts
        delegate: ItemDelegate {
            id: delegate
            width: parent ? parent.width : 0
            height: 150
            highlighted: ListView.isCurrentItem
            onClicked: connectToHost()

            function connectToHost() {
                Chiaki.connectToHost(index);
            }

            function wakeUpHost() {
                Chiaki.wakeUpHost(index);
            }

            function deleteHost() {
                if (!modelData.discovered)
                    root.showConfirmDialog(qsTr("Delete Console"), qsTr("Are you sure you want to delete this console?"), () => Chiaki.deleteHost(index));
            }

            RowLayout {
                anchors {
                    fill: parent
                    leftMargin: 30
                    rightMargin: 10
                    topMargin: 10
                    bottomMargin: 10
                }
                spacing: 50

                Image {
                    Layout.fillHeight: true
                    Layout.preferredWidth: 150
                    fillMode: Image.PreserveAspectFit
                    source: "image://svg/console-ps" + (modelData.ps5 ? "5" : "4") + (modelData.state == "standby" ? "#light_standby" : "#light_on")
                    sourceSize: Qt.size(width, height)
                }

                Label {
                    Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                    text: {
                        let t = "";
                        if (modelData.name)
                            t += modelData.name + "\n";
                        t += qsTr("Address: %1").arg(modelData.address);
                        if (modelData.mac)
                            t += "\n" + qsTr("ID: %1 (%2)").arg(modelData.mac).arg(modelData.registered ? qsTr("registered") : qsTr("unregistered"));
                        t += "\n" + (modelData.discovered ? qsTr("discovered") : qsTr("manual"));
                        return t;
                    }
                }

                Label {
                    Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                    text: {
                        let t = "";
                        if (!modelData.discovered)
                            return t;
                        t += qsTr("State: %1").arg(modelData.state);
                        if (modelData.app)
                            t += "\n" + qsTr("App: %1").arg(modelData.app);
                        if (modelData.titleId)
                            t += "\n" + qsTr("Title ID: %1").arg(modelData.titleId);
                        return t;
                    }
                }

                Item { Layout.fillWidth: true }

                ColumnLayout {
                    Layout.fillHeight: true
                    spacing: 0

                    Button {
                        Layout.alignment: Qt.AlignCenter
                        text: qsTr("Delete")
                        flat: true
                        padding: 20
                        leftPadding: delegate.highlighted ? 50 : undefined
                        focusPolicy: Qt.NoFocus
                        visible: !modelData.discovered
                        onClicked: delegate.deleteHost()
                        Material.roundedScale: Material.SmallScale

                        Image {
                            anchors {
                                left: parent.left
                                verticalCenter: parent.verticalCenter
                                leftMargin: 12
                            }
                            width: 28
                            height: 28
                            sourceSize: Qt.size(width, height)
                            source: root.controllerButton("box")
                            visible: delegate.highlighted
                        }
                    }

                    Button {
                        Layout.alignment: Qt.AlignCenter
                        text: qsTr("Wake Up")
                        flat: true
                        padding: 20
                        leftPadding: delegate.highlighted ? 50 : undefined
                        visible: modelData.registered && (!modelData.discovered || modelData.state == "standby")
                        focusPolicy: Qt.NoFocus
                        onClicked: delegate.wakeUpHost()
                        Material.roundedScale: Material.SmallScale

                        Image {
                            anchors {
                                left: parent.left
                                verticalCenter: parent.verticalCenter
                                leftMargin: 12
                            }
                            width: 28
                            height: 28
                            sourceSize: Qt.size(width, height)
                            source: root.controllerButton("pyramid")
                            visible: delegate.highlighted
                        }
                    }
                }
            }
        }
    }

    RoundButton {
        anchors {
            left: parent.left
            bottom: parent.bottom
            margins: 20
        }
        icon.source: "qrc:/icons/discover-" + (checked ? "" : "off-") + "24px.svg"
        icon.width: 50
        icon.height: 50
        padding: 20
        focusPolicy: Qt.NoFocus
        checkable: true
        checked: Chiaki.discoveryEnabled
        onToggled: Chiaki.discoveryEnabled = !Chiaki.discoveryEnabled
        Material.background: Material.accent
    }

    Image {
        anchors.centerIn: parent
        source: "qrc:/icons/chiaki.svg"
        sourceSize: Qt.size(Math.min(parent.width, parent.height) / 2.5, Math.min(parent.width, parent.height) / 2.5)
        opacity: 0.2
    }
}
