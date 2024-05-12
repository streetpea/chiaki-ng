import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaki4deck

import "controls" as C

DialogView {
    id: dialog
    title: qsTr("Settings")
    buttonVisible: false
    Keys.onPressed: (event) => {
        if (event.modifiers)
            return;
        switch (event.key) {
        case Qt.Key_PageUp:
            bar.decrementCurrentIndex();
            event.accepted = true;
            break;
        case Qt.Key_PageDown:
            bar.incrementCurrentIndex();
            event.accepted = true;
            break;
        }
    }

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
                text: qsTr("Audio/Wifi")
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

            TabButton {
                text: qsTr("PSN")
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
            onCurrentIndexChanged: nextItemInFocusChain().forceActiveFocus(Qt.TabFocusReason)

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

                    C.ComboBox {
                        Layout.preferredWidth: 400
                        firstInFocusChain: true
                        model: [qsTr("Do Nothing"), qsTr("Enter Sleep Mode"), qsTr("Ask")]
                        currentIndex: Chiaki.settings.disconnectAction
                        onActivated: index => Chiaki.settings.disconnectAction = index
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Action On Suspend:")
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 400
                        model: [qsTr("Do Nothing"), qsTr("Enter Sleep Mode")]
                        currentIndex: Chiaki.settings.suspendAction
                        onActivated: index => Chiaki.settings.suspendAction = index
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("PS5 Features:")
                    }

                    C.CheckBox {
                        text: qsTr("Haptics + adaptive triggers (DS), PS5 Rumble (others)")
                        checked: Chiaki.settings.dualSense
                        onToggled: Chiaki.settings.dualSense = checked
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Steam Deck Haptics:")
                        visible: (typeof Chiaki.settings.steamDeckHaptics !== "undefined") && (Chiaki.settings.dualSense === true)
                    }

                    C.CheckBox {
                        text: qsTr("True haptics for SteamDeck, better quality but noisier")
                        checked: {
                            if(typeof Chiaki.settings.steamDeckHaptics !== "undefined")
                                Chiaki.settings.steamDeckHaptics
                            else
                                false
                        }
                        onToggled: Chiaki.settings.steamDeckHaptics = checked
                        visible: (typeof Chiaki.settings.steamDeckHaptics !== "undefined") && (Chiaki.settings.dualSense === true)
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Buttons By Position:")
                    }

                    C.CheckBox {
                        text: qsTr("Use buttons by position instead of by label")
                        checked: Chiaki.settings.buttonsByPosition
                        onToggled: Chiaki.settings.buttonsByPosition = checked
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Steam Deck Vertical:")
                        visible: typeof Chiaki.settings.verticalDeck !== "undefined"
                    }

                    C.CheckBox {
                        text: qsTr("Use Steam Deck in vertical orientation (motion controls)")
                        checked: {
                            if(typeof Chiaki.settings.verticalDeck !== "undefined")
                                Chiaki.settings.verticalDeck
                            else
                                false
                        }
                        onToggled: Chiaki.settings.verticalDeck = checked
                        visible: typeof Chiaki.settings.verticalDeck !== "undefined"
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Verbose Logging:")
                    }

                    C.CheckBox {
                        text: qsTr("Warning: Don't enable for regular use")
                        checked: Chiaki.settings.logVerbose
                        onToggled: Chiaki.settings.logVerbose = checked
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

                        C.Button {
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

                C.Button {
                    id: aboutButton
                    anchors {
                        right: parent.right
                        rightMargin: 100
                        top: parent.top
                        topMargin: 50
                    }
                    lastInFocusChain: true
                    implicitWidth: 200
                    topPadding: 26
                    leftPadding: 30
                    rightPadding: 30
                    bottomPadding: 26
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

                    C.ComboBox {
                        Layout.preferredWidth: 400
                        firstInFocusChain: true
                        model: [qsTr("360p"), qsTr("540p"), qsTr("720p"), qsTr("1080p (PS5 and PS4 Pro)")]
                        currentIndex: Chiaki.settings.resolution - 1
                        onActivated: (index) => Chiaki.settings.resolution = index + 1
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("FPS:")
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 400
                        model: [qsTr("30 fps"), qsTr("60 fps")]
                        currentIndex: (Chiaki.settings.fps / 30) - 1
                        onActivated: (index) => Chiaki.settings.fps = (index + 1) * 30
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Bitrate:")
                    }

                    C.TextField {
                        Layout.preferredWidth: 400
                        text: Chiaki.settings.bitrate || ""
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
                            return num >= 2000 && num <= 99999;
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Codec:")
                    }

                    C.ComboBox {
                        id: codec
                        Layout.preferredWidth: 400
                        model: [qsTr("H264"), qsTr("H265 (PS5)"), qsTr("H265 HDR (PS5)")]
                        currentIndex: Chiaki.settings.codec
                        onActivated: (index) => Chiaki.settings.codec = index
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Hardware Decoder:")
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 400
                        model: Chiaki.settings.availableDecoders
                        currentIndex: Math.max(0, model.indexOf(Chiaki.settings.decoder))
                        onActivated: (index) => Chiaki.settings.decoder = index ? model[index] : ""
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Window Type:")
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 400
                        model: [qsTr("Selected Resolution"), qsTr("Fullscreen"), qsTr("Zoom [adjust zoom using slider in stream menu]"), qsTr("Stretch")]
                        currentIndex: Chiaki.settings.windowType
                        onActivated: (index) => Chiaki.settings.windowType = index;
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Render Preset:")
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 400
                        model: [qsTr("Fast"), qsTr("Default"), qsTr("High Quality")]
                        currentIndex: Chiaki.settings.videoPreset
                        onActivated: (index) => {
                            Chiaki.settings.videoPreset = index;
                            switch (index) {
                            case 0: Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.Fast; break;
                            case 1: Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.Default; break;
                            case 2: Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.HighQuality; break;
                            }
                        }
                    }
                }
            }

            Item {
                // Audio and Wifi
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

                    C.ComboBox {
                        Layout.preferredWidth: 400
                        popup.x: (width - popup.width) / 2
                        popup.width: 700
                        popup.font.pixelSize: 16
                        firstInFocusChain: true
                        model: [qsTr("Auto")].concat(Chiaki.settings.availableAudioOutDevices)
                        currentIndex: Math.max(0, model.indexOf(Chiaki.settings.audioOutDevice))
                        onActivated: (index) => Chiaki.settings.audioOutDevice = index ? model[index] : ""
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Input Device:")
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 400
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

                    C.TextField {
                        Layout.preferredWidth: 400
                        text: Chiaki.settings.audioBufferSize || ""
                        placeholderText: qsTr("Default (5760)")
                        Material.accent: text && !validate() ? Material.Red : undefined
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
                            return num >= 1920 && num <= 19200;
                        }
                    }


                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Start Mic Unmuted:")
                    }

                    C.CheckBox {
                        checked: Chiaki.settings.startMicUnmuted
                        onToggled: Chiaki.settings.startMicUnmuted = checked
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Speech Processing:")
                        visible: typeof Chiaki.settings.speechProcessing !== "undefined"
                    }

                    C.CheckBox {
                        lastInFocusChain: !checked
                        text: qsTr("Noise suppression + echo cancellation")
                        checked: Chiaki.settings.speechProcessing
                        onToggled: Chiaki.settings.speechProcessing = !Chiaki.settings.speechProcessing
                        visible: typeof Chiaki.settings.speechProcessing !== "undefined"
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Noise To Suppress:")
                        visible: if (typeof Chiaki.settings.speechProcessing !== "undefined") {Chiaki.settings.speechProcessing} else {false}
                    }

                    C.Slider {
                        Layout.preferredWidth: 250
                        from: 0
                        to: 60
                        stepSize: 1
                        visible: if (typeof Chiaki.settings.speechProcessing !== "undefined") {Chiaki.settings.speechProcessing} else {false}
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
                        visible: if (typeof Chiaki.settings.speechProcessing !== "undefined") {Chiaki.settings.speechProcessing} else {false}
                    }

                    C.Slider {
                        Layout.preferredWidth: 250
                        from: 0
                        to: 60
                        stepSize: 1
                        value: Chiaki.settings.echoSuppressLevel
                        visible: if (typeof Chiaki.settings.speechProcessing !== "undefined") {Chiaki.settings.speechProcessing} else {false}
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

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Wifi Instability Notification Triggers:")
                    }

                    C.Slider {
                        Layout.preferredWidth: 250
                        lastInFocusChain: true
                        from: 0
                        to: 100
                        stepSize: 1
                        value: Chiaki.settings.wifiDroppedNotif
                        onMoved: Chiaki.settings.wifiDroppedNotif = value

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: qsTr("%1 % dropped packets").arg(parent.value)
                        }
                    }
                }
            }

            Item {
                // Consoles
                C.Button {
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
                    firstInFocusChain: true
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
                    id: consolesView
                    anchors {
                        top: consolesLabel.bottom
                        horizontalCenter: consolesLabel.horizontalCenter
                        bottom: parent.bottom
                        topMargin: 10
                    }
                    width: 700
                    clip: true
                    model: Chiaki.settings.registeredHosts
                    delegate: ItemDelegate {
                        text: "%1 (%2, %3)".arg(modelData.mac).arg(modelData.ps5 ? "PS5" : "PS4").arg(modelData.name)
                        height: 80
                        width: parent ? parent.width : 0
                        leftPadding: autoConnectButton.width + 40

                        C.CheckBox {
                            id: autoConnectButton
                            anchors {
                                left: parent.left
                                verticalCenter: parent.verticalCenter
                                leftMargin: 20
                            }
                            text: qsTr("Auto-Connect")
                            checked: Chiaki.settings.autoConnectMac == modelData.mac
                            onToggled: Chiaki.settings.autoConnectMac = checked ? modelData.mac : "";
                        }

                        C.Button {
                            anchors {
                                right: parent.right
                                verticalCenter: parent.verticalCenter
                                rightMargin: 20
                            }
                            lastInFocusChain: index == consolesView.count - 1
                            text: qsTr("Delete")
                            onClicked: root.showConfirmDialog(qsTr("Delete Console"), qsTr("Are you sure you want to delete this console?"), () => Chiaki.settings.deleteRegisteredHost(index));
                            Material.roundedScale: Material.SmallScale
                            Material.accent: Material.Red
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
                    Button {
                        focusPolicy: Qt.NoFocus
                        text: "Reset All Keys"
                        onClicked: Chiaki.settings.clearKeyMapping()
                    }
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

            Item {
                // PSN Remote Connection Setup
                GridLayout {
                    anchors {
                        top: parent.top
                        horizontalCenter: parent.horizontalCenter
                        topMargin: 50
                    }
                    columns: 3
                    rowSpacing: 10
                    columnSpacing: 10

                    C.Button {
                        id: openPsnLogin
                        firstInFocusChain: true
                        text: qsTr("Login to PSN")
                        onClicked: {
                            showPSNTokenDialog(Chiaki.openPsnLink(), false)
                        }
                        Material.roundedScale: Material.SmallScale
                        visible: !Chiaki.settings.psnRefreshToken || !Chiaki.settings.psnAuthToken || !Chiaki.settings.psnAuthTokenExpiry || !Chiaki.settings.psnAccountId
                    }

                    C.Button {
                        id: resetPsnTokens
                        topPadding: 26
                        leftPadding: 30
                        rightPadding: 30
                        bottomPadding: 26
                        firstInFocusChain: true
                        text: qsTr("Clear PSN Token")
                        onClicked: {
                            Chiaki.settings.psnRefreshToken = ""
                            Chiaki.settings.psnAuthToken = ""
                            Chiaki.settings.psnAuthTokenExpiry = ""
                            Chiaki.settings.psnAccountId = ""
                        }
                        Material.roundedScale: Material.SmallScale
                        visible: Chiaki.settings.psnRefreshToken && Chiaki.settings.psnAuthToken && Chiaki.settings.psnAuthTokenExpiry && Chiaki.settings.psnAccountId
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
            onAboutToHide: aboutButton.forceActiveFocus(Qt.TabFocusReason)

            RowLayout {
                spacing: 50
                onVisibleChanged: if (visible) forceActiveFocus()
                Keys.onReturnPressed: aboutDialog.close()
                Keys.onEscapePressed: aboutDialog.close()

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
            closePolicy: Popup.CloseOnPressOutside
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
