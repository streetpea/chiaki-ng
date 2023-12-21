import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaki4deck

DialogView {
    id: dialog
    title: qsTr("Settings")
    buttonVisible: false

    Item {
        TabBar {
            id: bar
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
                topMargin: 5
            }

            TabButton {
                text: qsTr("General")
                focusPolicy: Qt.NoFocus
            }

            TabButton {
                text: qsTr("Video")
                focusPolicy: Qt.NoFocus
            }

            TabButton {
                text: qsTr("Audio")
                focusPolicy: Qt.NoFocus
            }

            TabButton {
                text: qsTr("Consoles")
                focusPolicy: Qt.NoFocus
            }

            TabButton {
                text: qsTr("Keys")
                focusPolicy: Qt.NoFocus
            }
        }

        StackLayout {
            anchors {
                top: bar.bottom
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            currentIndex: bar.currentIndex
            onCurrentIndexChanged: dialog.forceActiveFocus()

            Item {
                // General
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
                        text: qsTr("Action On Disconnect:")
                    }

                    ComboBox {
                        Layout.preferredWidth: 400
                        focusPolicy: Qt.NoFocus
                        model: [qsTr("Do Nothing"), qsTr("Enter Sleep Mode"), qsTr("Ask")]
                        currentIndex: Chiaki.settings.disconnectAction
                        onActivated: index => Chiaki.settings.disconnectAction = index
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Verbose Logging:")
                    }

                    CheckBox {
                        focusPolicy: Qt.NoFocus
                        text: qsTr("Warning: Don't enable for regular use")
                        checked: Chiaki.settings.logVerbose
                        onToggled: Chiaki.settings.logVerbose = checked
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("PS5 Features:")
                    }

                    CheckBox {
                        focusPolicy: Qt.NoFocus
                        text: qsTr("DualSense and Steam Deck haptics and adaptive triggers")
                        checked: Chiaki.settings.dualSense
                        onToggled: Chiaki.settings.dualSense = checked
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Buttons By Position:")
                    }

                    CheckBox {
                        focusPolicy: Qt.NoFocus
                        text: qsTr("Use buttons by position instead of by label")
                        checked: Chiaki.settings.buttonsByPosition
                        onToggled: Chiaki.settings.buttonsByPosition = checked
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Steam Deck Vertical:")
                    }

                    CheckBox {
                        focusPolicy: Qt.NoFocus
                        text: qsTr("Use Steam Deck in vertical orientation (motion controls)")
                        checked: Chiaki.settings.verticalDeck
                        onToggled: Chiaki.settings.verticalDeck = checked
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Log Directory:")
                    }

                    Label {
                        Layout.maximumWidth: 600
                        text: Chiaki.settings.logDirectory
                        verticalAlignment: Text.AlignVCenter
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 10

                        Button {
                            anchors {
                                left: parent.left
                                verticalCenter: parent.verticalCenter
                                leftMargin: parent.paintedWidth + 20
                            }
                            text: qsTr("Open")
                            onClicked: Qt.openUrlExternally("file://" + parent.text);
                            Material.roundedScale: Material.SmallScale
                        }
                    }
                }

                Button {
                    anchors {
                        bottom: parent.bottom
                        horizontalCenter: parent.horizontalCenter
                        bottomMargin: 50
                    }
                    implicitWidth: 200
                    topPadding: 26
                    leftPadding: 30
                    rightPadding: 30
                    bottomPadding: 26
                    focusPolicy: Qt.NoFocus
                    text: qsTr("About %1").arg(Qt.application.name)
                    onClicked: aboutDialog.open()
                    Material.roundedScale: Material.SmallScale
                }
            }

            Item {
                // Video
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
                        text: qsTr("Resolution:")
                    }

                    ComboBox {
                        Layout.preferredWidth: 400
                        focusPolicy: Qt.NoFocus
                        model: [qsTr("360p"), qsTr("540p"), qsTr("720p"), qsTr("1080p (PS5 and PS4 Pro)")]
                        currentIndex: Chiaki.settings.resolution - 1
                        onActivated: (index) => Chiaki.settings.resolution = index + 1
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("FPS:")
                    }

                    ComboBox {
                        Layout.preferredWidth: 400
                        focusPolicy: Qt.NoFocus
                        model: [qsTr("30 fps"), qsTr("60 fps")]
                        currentIndex: (Chiaki.settings.fps / 30) - 1
                        onActivated: (index) => Chiaki.settings.fps = (index + 1) * 30
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Bitrate:")
                    }

                    TextField {
                        Layout.preferredWidth: 400
                        text: Chiaki.settings.bitrate || ""
                        validator: IntValidator { bottom: 0; top: 99999; }
                        placeholderText: {
                            var bitrate = 0;
                            switch (Chiaki.settings.resolution) {
                            case 1: bitrate = 2000; break; // 360p
                            case 2: bitrate = 6000; break; // 540p
                            case 3: bitrate = 10000; break; // 720p
                            case 4: bitrate = 15000; break; // 1080p
                            }
                            return qsTr("Automatic (%1)").arg(bitrate);
                        }
                        Material.accent: text && !validate() ? Material.Red : undefined
                        onAccepted: dialog.forceActiveFocus()
                        onEditingFinished: {
                            if (validate()) {
                                Chiaki.settings.bitrate = parseInt(text);
                            } else {
                                Chiaki.settings.bitrate = 0;
                                text = "";
                            }
                        }
                        function validate() {
                            var num = parseInt(text);
                            return num >= 2000 && num <= 50000;
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Codec:")
                    }

                    ComboBox {
                        Layout.preferredWidth: 400
                        focusPolicy: Qt.NoFocus
                        model: [qsTr("H264"), qsTr("H265 (PS5)"), qsTr("H265 HDR (PS5)")]
                        currentIndex: Chiaki.settings.codec
                        onActivated: (index) => Chiaki.settings.codec = index
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Hardware Decoder:")
                    }

                    ComboBox {
                        Layout.preferredWidth: 400
                        focusPolicy: Qt.NoFocus
                        model: Chiaki.settings.availableDecoders
                        currentIndex: Math.max(0, model.indexOf(Chiaki.settings.decoder))
                        onActivated: (index) => Chiaki.settings.decoder = index ? model[index] : ""
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Render Preset:")
                    }

                    ComboBox {
                        Layout.preferredWidth: 400
                        focusPolicy: Qt.NoFocus
                        model: [qsTr("Default"), qsTr("Fast"), qsTr("High Quality")]
                        currentIndex: Chiaki.settings.videoPreset
                        onActivated: (index) => Chiaki.settings.videoPreset = index
                    }
                }
            }

            Item {
                // Audio
                GridLayout {
                    anchors {
                        top: parent.top
                        horizontalCenter: parent.horizontalCenter
                        topMargin: 20
                    }
                    columns: 2
                    rowSpacing: 10
                    columnSpacing: 20
                    onVisibleChanged: if (visible) Chiaki.settings.refreshAudioDevices()

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Output Device:")
                    }

                    ComboBox {
                        id: audioOutDevice
                        Layout.preferredWidth: 400
                        focusPolicy: Qt.NoFocus
                        popup.x: (width - popup.width) / 2
                        popup.width: 700
                        popup.font.pixelSize: 16
                        model: [qsTr("Auto")].concat(Chiaki.settings.availableAudioOutDevices)
                        currentIndex: Math.max(0, model.indexOf(Chiaki.settings.audioOutDevice))
                        onActivated: (index) => Chiaki.settings.audioOutDevice = index ? model[index] : ""
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Input Device:")
                    }

                    ComboBox {
                        Layout.preferredWidth: 400
                        focusPolicy: Qt.NoFocus
                        popup.x: (width - popup.width) / 2
                        popup.width: 700
                        popup.font.pixelSize: 16
                        model: [qsTr("Auto")].concat(Chiaki.settings.availableAudioInDevices)
                        currentIndex: Math.max(0, model.indexOf(Chiaki.settings.audioInDevice))
                        onActivated: (index) => Chiaki.settings.audioInDevice = index ? model[index] : ""
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Buffer Size:")
                    }

                    TextField {
                        Layout.preferredWidth: 400
                        validator: IntValidator { bottom: 0; top: 999999; }
                        text: Chiaki.settings.audioBufferSize || ""
                        placeholderText: qsTr("Default (9600)")
                        Material.accent: text && !validate() ? Material.Red : undefined
                        onAccepted: dialog.forceActiveFocus()
                        onEditingFinished: {
                            if (validate()) {
                                Chiaki.settings.audioBufferSize = parseInt(text);
                            } else {
                                Chiaki.settings.audioBufferSize = 0;
                                text = "";
                            }
                        }
                        function validate() {
                            var num = parseInt(text);
                            return num >= 1024 && num <= 0x20000;
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Speech Processing:")
                    }

                    CheckBox {
                        focusPolicy: Qt.NoFocus
                        text: qsTr("Noise suppression + echo cancellation")
                        checked: Chiaki.settings.speechProcessing
                        onToggled: Chiaki.settings.speechProcessing = checked
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Noise To Suppress:")
                        visible: Chiaki.settings.speechProcessing
                    }

                    Slider {
                        Layout.preferredWidth: 250
                        from: 0
                        to: 60
                        stepSize: 1
                        focusPolicy: Qt.NoFocus
                        visible: Chiaki.settings.speechProcessing
                        value: Chiaki.settings.noiseSuppressLevel
                        onMoved: Chiaki.settings.noiseSuppressLevel = value

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: qsTr("%1 dB").arg(parent.value)
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Echo To Suppress:")
                        visible: Chiaki.settings.speechProcessing
                    }

                    Slider {
                        Layout.preferredWidth: 250
                        from: 0
                        to: 60
                        stepSize: 1
                        focusPolicy: Qt.NoFocus
                        value: Chiaki.settings.echoSuppressLevel
                        visible: Chiaki.settings.speechProcessing
                        onMoved: Chiaki.settings.echoSuppressLevel = value

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: qsTr("%1 dB").arg(parent.value)
                        }
                    }
                }
            }

            Item {
                // Consoles
                Button {
                    id: registerNewButton
                    anchors {
                        top: parent.top
                        horizontalCenter: parent.horizontalCenter
                        topMargin: 30
                    }
                    topPadding: 26
                    leftPadding: 30
                    rightPadding: 30
                    bottomPadding: 26
                    focusPolicy: Qt.NoFocus
                    text: qsTr("Register New")
                    onClicked: root.showRegistDialog("255.255.255.255", true)
                    Material.roundedScale: Material.SmallScale
                }

                Label {
                    id: consolesLabel
                    anchors {
                        top: registerNewButton.bottom
                        horizontalCenter: registerNewButton.horizontalCenter
                        topMargin: 50
                    }
                    text: qsTr("Registered Consoles")
                    font.bold: true
                }

                ListView {
                    anchors {
                        top: consolesLabel.bottom
                        horizontalCenter: consolesLabel.horizontalCenter
                        bottom: parent.bottom
                        topMargin: 10
                    }
                    width: 500
                    clip: true
                    model: Chiaki.settings.registeredHosts
                    delegate: ItemDelegate {
                        text: "%1 (%2, %3)".arg(modelData.mac).arg(modelData.ps5 ? "PS5" : "PS4").arg(modelData.name)
                        height: 80
                        width: parent ? parent.width : 0

                        Button {
                            anchors {
                                right: parent.right
                                verticalCenter: parent.verticalCenter
                                rightMargin: 20
                            }
                            text: qsTr("Delete")
                            onClicked: root.showConfirmDialog(qsTr("Delete Console"), qsTr("Are you sure you want to delete this console?"), () => Chiaki.settings.deleteRegisteredHost(index));
                            Material.roundedScale: Material.SmallScale
                        }
                    }
                }
            }

            Item {
                // Keys
                GridLayout {
                    anchors {
                        top: parent.top
                        horizontalCenter: parent.horizontalCenter
                        topMargin: 20
                    }
                    columns: 3
                    rowSpacing: 10
                    columnSpacing: 10

                    Repeater {
                        model: Chiaki.settings.controllerMapping

                        RowLayout {
                            spacing: 20

                            Label {
                                Layout.preferredWidth: 200
                                horizontalAlignment: Text.AlignRight
                                text: modelData.buttonName
                            }

                            Button {
                                Layout.preferredWidth: 170
                                Layout.preferredHeight: 52
                                focusPolicy: Qt.NoFocus
                                text: modelData.keyName
                                Material.roundedScale: Material.SmallScale
                                onClicked: {
                                    keyDialog.show({
                                        value: modelData.buttonValue,
                                        callback: (name) => text = name,
                                    });
                                }
                            }
                        }
                    }
                }
            }
        }

        Dialog {
            id: aboutDialog
            parent: Overlay.overlay
            x: Math.round((root.width - width) / 2)
            y: Math.round((root.height - height) / 2)
            title: qsTr("About %1").arg(Qt.application.name)
            modal: true
            standardButtons: Dialog.Ok
            Material.roundedScale: Material.MediumScale

            RowLayout {
                spacing: 50

                Image {
                    Layout.preferredWidth: 200
                    fillMode: Image.PreserveAspectFit
                    verticalAlignment: Image.AlignTop
                    source: "qrc:icons/chiaki4deck.svg"
                }

                Label {
                    Layout.preferredWidth: 400
                    verticalAlignment: Text.AlignTop
                    wrapMode: Text.Wrap
                    text: "<h1>chiaki4deck</h1> by Street Pea, version %1
                        <h2>Fork of Chiaki</h2> by Florian Markl at version 2.1.1

                        <p>This program is free software: you can redistribute it and/or modify
                        it under the terms of the GNU Affero General Public License version 3
                        as published by the Free Software Foundation.</p>

                        <p>This program is distributed in the hope that it will be useful,
                        but WITHOUT ANY WARRANTY; without even the implied warranty of
                        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
                        GNU General Public License for more details.</p>".arg(Qt.application.version)
                }
            }
        }

        Dialog {
            id: keyDialog
            property int buttonValue
            property var buttonCallback
            parent: Overlay.overlay
            x: Math.round((root.width - width) / 2)
            y: Math.round((root.height - height) / 2)
            title: qsTr("Key Capture")
            modal: true
            standardButtons: Dialog.Close
            onOpened: keyLabel.forceActiveFocus()
            onClosed: dialog.forceActiveFocus()
            Material.roundedScale: Material.MediumScale

            function show(opts) {
                buttonValue = opts.value;
                buttonCallback = opts.callback;
                open();
            }

            Label {
                id: keyLabel
                focus: true
                text: qsTr("Press any key to configure button or click close")
                Keys.onReleased: (event) => {
                    var name = Chiaki.settings.changeControllerKey(keyDialog.buttonValue, event.key);
                    keyDialog.buttonCallback(name);
                    keyDialog.close();
                }
            }
        }
    }
}
