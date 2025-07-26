import QtCore
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs

import org.streetpea.chiaking

import "controls" as C

DialogView {
    enum Console {
        PS4,
        PS5
    }
    property int selectedConsole: SettingsDialog.Console.PS5
    property bool quitControllerMapping: true
    id: dialog
    title: qsTr("Settings")
    header: qsTr("* Defaults in () to right of value or marked with (Default)")
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
        case Qt.Key_Up:
            if(bar.currentIndex == 3)
            {
                if(audiowifiScrollbar.position > 0.001)
                    audiowifiFlick.flick(0, 500);
            }
            else if (bar.currentIndex == 6)
            {
                if(controllersScrollbar.position > 0.001)
                    controllersFlick.flick(0, 500);
            }
            else
                return;
            event.accepted = true;
            break;
        case Qt.Key_Down:
            if(bar.currentIndex == 3)
            {
                if(audiowifiScrollbar.position < 1.0 - audiowifiScrollbar.size - 0.001)
                    audiowifiFlick.flick(0, -500);
            }
            else if(bar.currentIndex == 6)
            {
                if(controllersScrollbar.position < 1.0 - controllersScrollbar.size - 0.001)
                    controllersFlick.flick(0, -500);
            }
            else
                return;
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
                id: general
                text: qsTr("General")
                focusPolicy: Qt.NoFocus
                Image {
                    anchors {
                        left: general.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/l1.svg"
                    visible: bar.currentIndex == 1
                }
            }

            TabButton {
                id: video
                text: qsTr("Video")
                focusPolicy: Qt.NoFocus
                Image {
                    anchors {
                        right: video.left
                        verticalCenter: parent.verticalCenter
                        rightMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/r1.svg"
                    visible: bar.currentIndex == 0
                }
                Image {
                    anchors {
                        left: video.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/l1.svg"
                    visible: bar.currentIndex == 2
                }
            }

            TabButton {
                id: stream
                text: qsTr("Stream")
                focusPolicy: Qt.NoFocus
                Image {
                    anchors {
                        right: stream.left
                        verticalCenter: parent.verticalCenter
                        rightMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/r1.svg"
                    visible: bar.currentIndex == 1
                }
                Image {
                    anchors {
                        left: stream.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/l1.svg"
                    visible: bar.currentIndex == 3
                }
            }

            TabButton {
                text: qsTr("Audio/Wifi")
                id: audio
                focusPolicy: Qt.NoFocus
                Image {
                    anchors {
                        right: audio.left
                        verticalCenter: parent.verticalCenter
                        rightMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/r1.svg"
                    visible: bar.currentIndex == 2
                }
                Image {
                    anchors {
                        left: audio.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/l1.svg"
                    visible: bar.currentIndex == 4
                }
            }

            TabButton {
                text: qsTr("Consoles")
                id: consoles
                focusPolicy: Qt.NoFocus
                Image {
                    anchors {
                        right: consoles.left
                        verticalCenter: parent.verticalCenter
                        rightMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/r1.svg"
                    visible: bar.currentIndex == 3
                }
                Image {
                    anchors {
                        left: consoles.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/l1.svg"
                    visible: bar.currentIndex == 5
                }
            }

            TabButton {
                text: qsTr("Keys")
                id: keys
                focusPolicy: Qt.NoFocus
                Image {
                    anchors {
                        right: keys.left
                        verticalCenter: parent.verticalCenter
                        rightMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/r1.svg"
                    visible: bar.currentIndex == 4
                }
                Image {
                    anchors {
                        left: keys.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/l1.svg"
                    visible: bar.currentIndex == 6
                }
            }

            TabButton {
                text: qsTr("Controllers")
                id: controllers
                focusPolicy: Qt.NoFocus
                Image {
                    anchors {
                        right: controllers.left
                        verticalCenter: parent.verticalCenter
                        rightMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/r1.svg"
                    visible: bar.currentIndex == 5
                }
                Image {
                    anchors {
                        left: controllers.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/l1.svg"
                    visible: bar.currentIndex == 7
                }
            }

            TabButton {
                text: qsTr("Config")
                id: config
                focusPolicy: Qt.NoFocus
                Image {
                    anchors {
                        right: config.left
                        verticalCenter: parent.verticalCenter
                        rightMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/r1.svg"
                    visible: bar.currentIndex == 6
                }
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
                ColumnLayout {
                    anchors {
                        top: parent.top
                        horizontalCenter: parent.horizontalCenter
                        topMargin: 20
                    }
                    spacing: 10
                    GridLayout {
                        Layout.alignment: Qt.AlignHCenter
                        columns: 3
                        rowSpacing: 5
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
                            text: qsTr("(Ask)")
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
                            text: qsTr("(Do Nothing)")
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("Steam Deck Haptics:")
                            visible: (typeof Chiaki.settings.steamDeckHaptics !== "undefined")
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
                            visible: (typeof Chiaki.settings.steamDeckHaptics !== "undefined")
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("(Unchecked)")
                            visible: (typeof Chiaki.settings.steamDeckHaptics !== "undefined")
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
                            text: qsTr("(Unchecked)")
                            visible: typeof Chiaki.settings.verticalDeck !== "undefined"
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("Audio/Video:")
                        }

                        C.ComboBox {
                            Layout.preferredWidth: 400
                            model: [qsTr("Audio and Video Enabled"), qsTr("Audio Disabled"), qsTr("Video Disabled"), qsTr("Audio and Video Disabled")]
                            currentIndex: Chiaki.settings.audioVideoDisabled
                            onActivated: index => Chiaki.settings.audioVideoDisabled = index
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("(Audio and Video Enabled)")
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
                                id: openButton
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
                        Label {

                        }
                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("Streamer Mode (Hides Info)")
                        }
                        C.CheckBox {
                            checked: Chiaki.settings.streamerMode
                            onToggled: Chiaki.settings.streamerMode = !Chiaki.settings.streamerMode
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("(Unchecked)")
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("Stream Menu Shortcut Enabled")
                        }
                        C.CheckBox {
                            id: streamMenu
                            checked: Chiaki.settings.streamMenuEnabled
                            onToggled: Chiaki.settings.streamMenuEnabled = !Chiaki.settings.streamMenuEnabled
                            KeyNavigation.priority: KeyNavigation.BeforeItem
                            KeyNavigation.up: openButton
                            KeyNavigation.left: streamMenu
                            KeyNavigation.right: streamMenu
                            KeyNavigation.down: {
                                if(streamMenuShortcut1.visible)
                                    streamMenuShortcut1
                                else
                                    streamMenu
                            }
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("(Checked)")
                        }
                    }
                    RowLayout {
                        spacing: 10
                        visible: Chiaki.settings.streamMenuEnabled
                        Layout.alignment: Qt.AlignHCenter
                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("Stream Menu Combo:")
                        }

                        C.ComboBox {
                            id: streamMenuShortcut1
                            implicitContentWidthPolicy: ComboBox.WidestText
                            firstInFocusChain: false
                            model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                            currentIndex: Chiaki.settings.streamMenuShortcut1
                            onActivated: index => Chiaki.settings.streamMenuShortcut1 = index
                            KeyNavigation.priority: {
                                if(!popup.visible)
                                    KeyNavigation.BeforeItem
                                else
                                    KeyNavigation.AfterItem
                            }
                            KeyNavigation.up: streamMenu
                            KeyNavigation.down: streamMenuShortcut1
                            KeyNavigation.left: streamMenuShortcut1
                            KeyNavigation.right: streamMenuShortcut2
                        }

                        C.ComboBox {
                            id: streamMenuShortcut2
                            implicitContentWidthPolicy: ComboBox.WidestText
                            firstInFocusChain: false
                            model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                            currentIndex: Chiaki.settings.streamMenuShortcut2
                            onActivated: index => Chiaki.settings.streamMenuShortcut2 = index
                            KeyNavigation.priority: {
                                if(!popup.visible)
                                    KeyNavigation.BeforeItem
                                else
                                    KeyNavigation.AfterItem
                            }
                            KeyNavigation.up: streamMenu
                            KeyNavigation.down: streamMenuShortcut2
                            KeyNavigation.left: streamMenuShortcut1
                            KeyNavigation.right: streamMenuShortcut3
                        }

                        C.ComboBox {
                            id: streamMenuShortcut3
                            implicitContentWidthPolicy: ComboBox.WidestText
                            firstInFocusChain: false
                            model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                            currentIndex: Chiaki.settings.streamMenuShortcut3
                            onActivated: index => Chiaki.settings.streamMenuShortcut3 = index
                            KeyNavigation.priority: {
                                if(!popup.visible)
                                    KeyNavigation.BeforeItem
                                else
                                    KeyNavigation.AfterItem
                            }
                            KeyNavigation.up: streamMenu
                            KeyNavigation.down: streamMenuShortcut3
                            KeyNavigation.left: streamMenuShortcut2
                            KeyNavigation.right: streamMenuShortcut4
                        }

                        C.ComboBox {
                            id: streamMenuShortcut4
                            implicitContentWidthPolicy: ComboBox.WidestText
                            firstInFocusChain: false
                            model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                            currentIndex: Chiaki.settings.streamMenuShortcut4
                            onActivated: index => Chiaki.settings.streamMenuShortcut4 = index
                            KeyNavigation.priority: {
                                if(!popup.visible)
                                    KeyNavigation.BeforeItem
                                else
                                    KeyNavigation.AfterItem
                            }
                            KeyNavigation.up: streamMenu
                            KeyNavigation.down: streamMenuShortcut4
                            KeyNavigation.left: streamMenuShortcut3
                            KeyNavigation.right: streamMenuShortcut4
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("(L1+R1+L3+R3)")
                        }
                    }
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
                    columns: 3
                    rowSpacing: 10
                    columnSpacing: 20

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
                        text: qsTr("(Auto)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Window Type:")
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 400
                        popup.width: 500
                        model: [qsTr("Stream Resolution"), qsTr("Custom Resolution"), qsTr("Adjust Resolution Manually"), qsTr("Fullscreen"), qsTr("Zoom [adjust zoom using slider in stream menu]"), qsTr("Stretch")]
                        currentIndex: Chiaki.settings.windowType
                        onActivated: (index) => Chiaki.settings.windowType = index;
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(Stream Resolution)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Custom Resolution Width")
                        visible: Chiaki.settings.windowType == 1
                    }

                    C.TextField {
                        id: customResolutionWidth
                        Layout.preferredWidth: 400
                        visible: Chiaki.settings.windowType == 1
                        text: Chiaki.settings.customResolutionWidth
                        Material.accent: text && !validate() ? Material.Red : undefined
                        onEditingFinished: {
                            if (validate()) {
                                Chiaki.settings.customResolutionWidth = parseInt(text);
                            } else {
                                Chiaki.settings.customResolutionWidth = 0;
                                text = "";
                            }
                        }
                        function validate() {
                            var num = parseInt(text);
                            return num >= 0 && num <= 9999;
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(1920)")
                        visible: Chiaki.settings.windowType == 1
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Custom Resolution Height")
                        visible: Chiaki.settings.windowType == 1
                    }

                    C.TextField {
                        id: customResolutionHeight
                        Layout.preferredWidth: 400
                        visible: Chiaki.settings.windowType == 1
                        text: Chiaki.settings.customResolutionHeight
                        Material.accent: text && !validate() ? Material.Red : undefined
                        onEditingFinished: {
                            if (validate()) {
                                Chiaki.settings.customResolutionHeight = parseInt(text);
                            } else {
                                Chiaki.settings.customResolutionHeight = 0;
                                text = "";
                            }
                        }
                        function validate() {
                            var num = parseInt(text);
                            return num >= 0 && num <= 9999;
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(1080)")
                        visible: Chiaki.settings.windowType == 1
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Toggle Fullscreen on Double-click:")
                    }

                    C.CheckBox {
                        checked: Chiaki.settings.fullscreenDoubleClick
                        onToggled: Chiaki.settings.fullscreenDoubleClick = checked
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(Unchecked)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Hide Cursor during Stream")
                    }
                    C.CheckBox {
                        checked: Chiaki.settings.hideCursor
                        onToggled: Chiaki.settings.hideCursor = !Chiaki.settings.hideCursor
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(Checked)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Render Preset:")
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 400
                        model: [qsTr("Fast"), qsTr("Default"), qsTr("High Quality"), qsTr("Custom")]
                        currentIndex: Chiaki.settings.videoPreset
                        onActivated: (index) => {
                            Chiaki.settings.videoPreset = index;
                            switch (index) {
                            case 0: Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.Fast; break;
                            case 1: Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.Default; break;
                            case 2: Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.HighQuality; break;
                            case 3: Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.Custom; break;
                            }
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(High Quality)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Custom Renderer Settings")
                        visible: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Custom
                    }

                    C.Button {
                        id: customRendererSettings
                        text: qsTr("Open")
                        onClicked: root.showPlaceboSettingsDialog()
                        Material.roundedScale: Material.SmallScale
                        visible: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Custom
                    }

                    Label { visible: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Custom }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Display Settings")
                    }

                    C.Button {
                        id: displaySettings
                        text: qsTr("Open")
                        onClicked: root.showDisplaySettingsDialog()
                        Material.roundedScale: Material.SmallScale
                        lastInFocusChain: true
                    }

                    Label {}
                }
            }

            Item {
                // Stream
                GridLayout {
                    anchors {
                        top: parent.top
                        horizontalCenter: parent.horizontalCenter
                        topMargin: 20
                    }
                    columns: 3
                    rowSpacing: 10
                    columnSpacing: 20

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Settings for:")
                    }


                    C.ComboBox {
                        id: consoleSelection
                        Layout.preferredWidth: 400
                        Layout.alignment: Qt.AlignLeft
                        model: [qsTr("PS4"), qsTr("PS5")]
                        currentIndex: selectedConsole
                        onActivated: (index) => selectedConsole = index
                        firstInFocusChain: true
                        KeyNavigation.priority: {
                            if(!popup.visible)
                                KeyNavigation.BeforeItem
                            else
                                KeyNavigation.AfterItem
                        }
                        KeyNavigation.down: {
                            if(selectedConsole == SettingsDialog.Console.PS4)
                                resolutionLocalPS4
                            else
                                resolutionLocalPS5

                        }

                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("")
                    }

                    Label {
                        Layout.alignment: Qt.AlignCenter
                        text: qsTr("Local")
                    }

                    Label {
                        Layout.alignment: Qt.AlignCenter
                        text: qsTr("Remote")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Resolution:")
                    }

                    C.ComboBox {
                        id: resolutionLocalPS4
                        Layout.preferredWidth: 400
                        model: [qsTr("360p"), qsTr("540p"), qsTr("720p (Default)"), qsTr("1080p (PS5 and PS4 Pro)")]
                        currentIndex: Chiaki.settings.resolutionLocalPS4 - 1
                        onActivated: (index) => {
                            Chiaki.settings.resolutionLocalPS4 = index + 1
                            Chiaki.settings.bitrateLocalPS4 = 0
                        }
                        visible: selectedConsole == SettingsDialog.Console.PS4
                        KeyNavigation.right: resolutionRemotePS4
                        KeyNavigation.down: fpsLocalPS4
                        KeyNavigation.up: consoleSelection
                        KeyNavigation.priority: {
                            if(!popup.visible)
                                KeyNavigation.BeforeItem
                            else
                                KeyNavigation.AfterItem
                        }
                    }

                    C.ComboBox {
                        id: resolutionRemotePS4
                        Layout.preferredWidth: 400
                        model: [qsTr("360p"), qsTr("540p"), qsTr("720p (Default)"), qsTr("1080p (PS5 and PS4 Pro)")]
                        currentIndex: Chiaki.settings.resolutionRemotePS4 - 1
                        onActivated: (index) => {
                            Chiaki.settings.resolutionRemotePS4 = index + 1
                            Chiaki.settings.bitrateRemotePS4 = 0
                        }
                        visible: selectedConsole == SettingsDialog.Console.PS4
                        KeyNavigation.left: resolutionLocalPS4
                        KeyNavigation.down: fpsRemotePS4
                        KeyNavigation.up: consoleSelection
                        KeyNavigation.priority: {
                            if(!popup.visible)
                                KeyNavigation.BeforeItem
                            else
                                KeyNavigation.AfterItem
                        }
                    }

                    C.ComboBox {
                        id: resolutionLocalPS5
                        Layout.preferredWidth: 400
                        model: [qsTr("360p"), qsTr("540p"), qsTr("720p"), qsTr("1080p (Default)")]
                        currentIndex: Chiaki.settings.resolutionLocalPS5 - 1
                        onActivated: (index) => {
                            Chiaki.settings.resolutionLocalPS5 = index + 1
                            Chiaki.settings.bitrateLocalPS5 = 0
                        }
                        visible: selectedConsole == SettingsDialog.Console.PS5
                        KeyNavigation.right: resolutionRemotePS5
                        KeyNavigation.up: consoleSelection
                        KeyNavigation.down: fpsLocalPS5
                        KeyNavigation.priority: {
                            if(!popup.visible)
                                KeyNavigation.BeforeItem
                            else
                                KeyNavigation.AfterItem
                        }
                    }

                    C.ComboBox {
                        id: resolutionRemotePS5
                        Layout.preferredWidth: 400
                        model: [qsTr("360p"), qsTr("540p"), qsTr("720p (Default)"), qsTr("1080p")]
                        currentIndex: Chiaki.settings.resolutionRemotePS5 - 1
                        onActivated: (index) => {
                            Chiaki.settings.resolutionRemotePS5 = index + 1
                            Chiaki.settings.bitrateRemotePS5 = 0
                        }
                        visible: selectedConsole == SettingsDialog.Console.PS5
                        KeyNavigation.left: resolutionLocalPS5
                        KeyNavigation.up: consoleSelection
                        KeyNavigation.down: fpsRemotePS5
                        KeyNavigation.priority: {
                            if(!popup.visible)
                                KeyNavigation.BeforeItem
                            else
                                KeyNavigation.AfterItem
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("FPS:")
                    }

                    C.ComboBox {
                        id: fpsLocalPS4
                        Layout.preferredWidth: 400
                        model: [qsTr("30 fps"), qsTr("60 fps (Default)")]
                        currentIndex: (Chiaki.settings.fpsLocalPS4 / 30) - 1
                        onActivated: (index) => Chiaki.settings.fpsLocalPS4 = (index + 1) * 30
                        visible: selectedConsole == SettingsDialog.Console.PS4
                        KeyNavigation.up: resolutionLocalPS4
                        KeyNavigation.right: fpsRemotePS4
                        KeyNavigation.down: bitrateLocalPS4
                        KeyNavigation.priority: {
                            if(!popup.visible)
                                KeyNavigation.BeforeItem
                            else
                                KeyNavigation.AfterItem
                        }
                    }

                    C.ComboBox {
                        id: fpsRemotePS4
                        Layout.preferredWidth: 400
                        model: [qsTr("30 fps"), qsTr("60 fps (Default)")]
                        currentIndex: (Chiaki.settings.fpsRemotePS4 / 30) - 1
                        onActivated: (index) => Chiaki.settings.fpsRemotePS4 = (index + 1) * 30
                        visible: selectedConsole == SettingsDialog.Console.PS4
                        KeyNavigation.up: resolutionRemotePS4
                        KeyNavigation.left: fpsLocalPS4
                        KeyNavigation.down: bitrateRemotePS4
                        KeyNavigation.priority: {
                            if(!popup.visible)
                                KeyNavigation.BeforeItem
                            else
                                KeyNavigation.AfterItem
                        }
                    }

                    C.ComboBox {
                        id: fpsLocalPS5
                        Layout.preferredWidth: 400
                        model: [qsTr("30 fps"), qsTr("60 fps (Default)")]
                        currentIndex: (Chiaki.settings.fpsLocalPS5 / 30) - 1
                        onActivated: (index) => Chiaki.settings.fpsLocalPS5 = (index + 1) * 30
                        visible: selectedConsole == SettingsDialog.Console.PS5
                        KeyNavigation.up: resolutionLocalPS5
                        KeyNavigation.right: fpsRemotePS5
                        KeyNavigation.down: bitrateLocalPS5
                        KeyNavigation.priority: {
                            if(!popup.visible)
                                KeyNavigation.BeforeItem
                            else
                                KeyNavigation.AfterItem
                        }
                    }

                    C.ComboBox {
                        id: fpsRemotePS5
                        Layout.preferredWidth: 400
                        model: [qsTr("30 fps"), qsTr("60 fps (Default)")]
                        currentIndex: (Chiaki.settings.fpsRemotePS5 / 30) - 1
                        onActivated: (index) => Chiaki.settings.fpsRemotePS5 = (index + 1) * 30
                        visible: selectedConsole == SettingsDialog.Console.PS5
                        KeyNavigation.up: resolutionRemotePS5
                        KeyNavigation.left: fpsLocalPS5
                        KeyNavigation.down: bitrateRemotePS5
                        KeyNavigation.priority: {
                            if(!popup.visible)
                                KeyNavigation.BeforeItem
                            else
                                KeyNavigation.AfterItem
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Bitrate:")
                    }

                    C.Slider {
                        id: bitrateLocalPS4
                        visible: selectedConsole == SettingsDialog.Console.PS4
                        property var bitrate: {
                            var rate = 0;
                            switch (Chiaki.settings.resolutionLocalPS4) {
                            case 1: rate = 2; break; // 360p
                            case 2: rate = 6; break; // 540p
                            case 3: rate = 10; break; // 720p
                            case 4: rate = 15; break; // 1080p
                            }
                            return rate;
                        }
                        Layout.preferredWidth: 200
                        from: 2
                        to: 100
                        stepSize: 1
                        value: Chiaki.settings.bitrateLocalPS4 / 1000 ? (Chiaki.settings.bitrateLocalPS4 / 1000) : bitrate
                        onMoved: Chiaki.settings.bitrateLocalPS4 = value * 1000;
                        KeyNavigation.up: fpsLocalPS4
                        KeyNavigation.down: bitrateLocalPS4
                        KeyNavigation.priority: KeyNavigation.BeforeItem
                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: (parent.value) + qsTr(" Mbps") + qsTr(" (%1 Mbps)").arg(parent.bitrate.toFixed(0))
                        }
                    }

                    C.Slider {
                        id: bitrateRemotePS4
                        visible: selectedConsole == SettingsDialog.Console.PS4
                        property var bitrate: {
                            var rate = 0;
                            switch (Chiaki.settings.resolutionRemotePS4) {
                            case 1: rate = 2; break; // 360p
                            case 2: rate = 6; break; // 540p
                            case 3: rate = 10; break; // 720p
                            case 4: rate = 15; break; // 1080p
                            }
                            return rate;
                        }
                        Layout.preferredWidth: 200
                        from: 2
                        to: 100
                        stepSize: 1
                        value: Chiaki.settings.bitrateRemotePS4 / 1000 ? (Chiaki.settings.bitrateRemotePS4 / 1000) : bitrate
                        onMoved: Chiaki.settings.bitrateRemotePS4 = value * 1000;
                        KeyNavigation.up: fpsRemotePS4
                        KeyNavigation.down: bitrateRemotePS4
                        KeyNavigation.priority: KeyNavigation.BeforeItem
                        lastInFocusChain: true

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: (parent.value) + qsTr(" Mbps") + qsTr(" (%1 Mbps)").arg(parent.bitrate.toFixed(0))
                        }
                    }

                    C.Slider {
                        id: bitrateLocalPS5
                        visible: selectedConsole == SettingsDialog.Console.PS5
                        property var bitrate: {
                            var rate = 0;
                            switch (Chiaki.settings.resolutionLocalPS5) {
                            case 1: rate = 2; break; // 360p
                            case 2: rate = 6; break; // 540p
                            case 3: rate = 10; break; // 720p
                            case 4: rate = 15; break; // 1080p
                            }
                            return rate;
                        }
                        Layout.preferredWidth: 200
                        from: 2
                        to: 100
                        stepSize: 1
                        value: Chiaki.settings.bitrateLocalPS5 / 1000 ? (Chiaki.settings.bitrateLocalPS5 / 1000) : bitrate
                        onMoved: Chiaki.settings.bitrateLocalPS5 = value * 1000;
                        KeyNavigation.up: fpsLocalPS5
                        KeyNavigation.down: codecLocalPS5
                        KeyNavigation.priority: KeyNavigation.BeforeItem

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: (parent.value) + qsTr(" Mbps") + qsTr(" (%1 Mbps)").arg(parent.bitrate.toFixed(0))
                        }
                    }

                    C.Slider {
                        id: bitrateRemotePS5
                        visible: selectedConsole == SettingsDialog.Console.PS5
                        property var bitrate: {
                            var rate = 0;
                            switch (Chiaki.settings.resolutionRemotePS5) {
                            case 1: rate = 2; break; // 360p
                            case 2: rate = 6; break; // 540p
                            case 3: rate = 10; break; // 720p
                            case 4: rate = 15; break; // 1080p
                            }
                            return rate;
                        }
                        Layout.preferredWidth: 200
                        from: 2
                        to: 100
                        stepSize: 1
                        value: Chiaki.settings.bitrateRemotePS5 / 1000 ? (Chiaki.settings.bitrateRemotePS5 / 1000) : bitrate
                        onMoved: Chiaki.settings.bitrateRemotePS5 = value * 1000;
                        KeyNavigation.up: fpsRemotePS5
                        KeyNavigation.down: codecRemotePS5
                        KeyNavigation.priority: KeyNavigation.BeforeItem
                        lastInFocusChain: true

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: (parent.value) + qsTr(" Mbps") + qsTr(" (%1 Mbps)").arg(parent.bitrate.toFixed(0))
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Codec:")
                        visible: selectedConsole == SettingsDialog.Console.PS5
                    }

                    C.ComboBox {
                        id: codecLocalPS5
                        Layout.preferredWidth: 400
                        model: [qsTr("H264"), qsTr("H265 (Default)"), qsTr("H265 HDR")]
                        currentIndex: Chiaki.settings.codecLocalPS5
                        onActivated: (index) => Chiaki.settings.codecLocalPS5 = index
                        visible: selectedConsole == SettingsDialog.Console.PS5
                        Keys.onReturnPressed: {
                            if (popup.visible) {
                                activated(highlightedIndex);
                                popup.close();
                            } else
                                popup.open();
                        }
                        KeyNavigation.up: bitrateLocalPS5
                        KeyNavigation.right: codecRemotePS5
                        KeyNavigation.down: codecLocalPS5
                        KeyNavigation.priority: {
                            if(!popup.visible)
                                KeyNavigation.BeforeItem
                            else
                                KeyNavigation.AfterItem
                        }
                    }

                    C.ComboBox {
                        id: codecRemotePS5
                        Layout.preferredWidth: 400
                        model: [qsTr("H264"), qsTr("H265 (Default)"), qsTr("H265 HDR")]
                        currentIndex: Chiaki.settings.codecRemotePS5
                        onActivated: (index) => Chiaki.settings.codecRemotePS5 = index
                        visible: selectedConsole == SettingsDialog.Console.PS5
                        lastInFocusChain: true
                        Keys.onReturnPressed: {
                            if (popup.visible) {
                                activated(highlightedIndex);
                                popup.close();
                            } else
                                popup.open();
                        }
                        KeyNavigation.up: bitrateRemotePS5
                        KeyNavigation.left: codecLocalPS5
                        KeyNavigation.priority: {
                            if(!popup.visible)
                                KeyNavigation.BeforeItem
                            else
                                KeyNavigation.AfterItem
                        }
                    }
                }
            }

            Item {
                // Audio and Wifi
                Flickable {
                    id: audiowifiFlick
                    implicitWidth: parent.width ? parent.width: 0
                    implicitHeight: parent.height ? parent.height: 0
                    anchors {
                        fill: parent
                        topMargin: 20
                        bottomMargin: 20
                        leftMargin: parent.width ? (parent.width / 2 - audiowifigrid.width / 2) : 0
                    }
                    clip: true
                    contentWidth: audiowifigrid.width
                    contentHeight: audiowifigrid.height
                    flickableDirection: Flickable.AutoFlickIfNeeded
                    ScrollBar.vertical: ScrollBar {
                        id: audiowifiScrollbar
                        policy: ScrollBar.AlwaysOn
                        visible: audiowifiFlick.contentHeight > audiowifiFlick.implicitHeight - audiowifiFlick.anchors.topMargin - audiowifiFlick.anchors.bottomMargin
                    }
                    GridLayout {
                        id: audiowifigrid
                        columns: 3
                        rowSpacing: 10
                        columnSpacing: 20
                        onVisibleChanged: if (visible) Chiaki.settings.refreshAudioDevices()

                        anchors {
                            top: parent.top
                            horizontalCenter: parent.horizontalCenter
                        }
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
                            Keys.onPressed: (event) => {
                                if (event.modifiers)
                                    return;
                                switch (event.key) {
                                    case Qt.Key_Up:
                                        if(bar.currentIndex != 3)
                                            return;
                                        if(audiowifiScrollbar.position > 0.001)
                                            audiowifiFlick.flick(0, 500);
                                        event.accepted = true;
                                        break;
                                    case Qt.Key_Down:
                                        if(bar.currentIndex != 3)
                                            return;
                                        if(audiowifiScrollbar.position < 1.0 - audiowifiScrollbar.size - 0.001)
                                            audiowifiFlick.flick(0, -500);
                                        event.accepted = true;
                                        break;
                                }
                            }
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("(Auto)")
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
                            Keys.onPressed: (event) => {
                                if (event.modifiers)
                                    return;
                                switch (event.key) {
                                    case Qt.Key_Up:
                                        if(bar.currentIndex != 3)
                                            return;
                                        if(audiowifiScrollbar.position > 0.001)
                                            audiowifiFlick.flick(0, 500);
                                        event.accepted = true;
                                        break;
                                    case Qt.Key_Down:
                                        if(bar.currentIndex != 3)
                                            return;
                                        if(audiowifiScrollbar.position < 1.0 - audiowifiScrollbar.size - 0.001)
                                            audiowifiFlick.flick(0, -500);
                                        event.accepted = true;
                                        break;
                                }
                            }
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("(Auto)")
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("Audio Buffer Size:")
                        }

                        C.Slider {
                            Layout.preferredWidth: 250
                            from: 1
                            to: 10
                            stepSize: 1
                            value: Chiaki.settings.audioBufferSize / 1920 ? (Chiaki.settings.audioBufferSize / 1920) : 5
                            onMoved: Chiaki.settings.audioBufferSize = value * 1920;
                            sendOutput: true

                            Label {
                                anchors {
                                    left: parent.right
                                    verticalCenter: parent.verticalCenter
                                    leftMargin: 10
                                }
                                text: {
                                    (parent.value * 10).toFixed(0) + qsTr(" ms")
                                }
                            }
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("(50 ms)")
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("Audio Volume:")
                        }

                        C.Slider {
                            Layout.preferredWidth: 250
                            from: 0
                            to: 128
                            stepSize: 1
                            value: Chiaki.settings.audioVolume
                            onMoved: Chiaki.settings.audioVolume = value
                            sendOutput: true

                            Label {
                                anchors {
                                    left: parent.right
                                    verticalCenter: parent.verticalCenter
                                    leftMargin: 10
                                }
                                text: {
                                    ((parent.value / 128.0) * 100).toFixed(0) + qsTr("% volume")
                                }
                            }
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("(100%)")
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("Start Mic Unmuted:")
                        }

                        C.CheckBox {
                            sendOutput: true
                            checked: Chiaki.settings.startMicUnmuted
                            onToggled: Chiaki.settings.startMicUnmuted = checked
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("(Unchecked)")
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("Speech Processing:")
                            visible: typeof Chiaki.settings.speechProcessing !== "undefined"
                        }

                        C.CheckBox {
                            sendOutput: true
                            text: qsTr("Noise suppression + echo cancellation")
                            checked: Chiaki.settings.speechProcessing
                            onToggled: Chiaki.settings.speechProcessing = !Chiaki.settings.speechProcessing
                            visible: typeof Chiaki.settings.speechProcessing !== "undefined"
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("(Unchecked)")
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
                            sendOutput: true
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
                            text: qsTr("(6 dB)")
                            visible: if (typeof Chiaki.settings.speechProcessing !== "undefined") {Chiaki.settings.speechProcessing} else {false}
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
                            sendOutput: true
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
                            text: qsTr("(30 dB)")
                            visible: if (typeof Chiaki.settings.speechProcessing !== "undefined") {Chiaki.settings.speechProcessing} else {false}
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("Weak Wifi Notification:")
                        }

                        C.Slider {
                            Layout.preferredWidth: 250
                            from: 0
                            to: 100
                            stepSize: 1
                            sendOutput: true
                            value: Chiaki.settings.wifiDroppedNotif
                            onMoved: Chiaki.settings.wifiDroppedNotif = value

                            Label {
                                anchors {
                                    left: parent.right
                                    verticalCenter: parent.verticalCenter
                                    leftMargin: 10
                                }
                                text: qsTr(">= %1% dropped packets").arg(parent.value)
                            }
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("(3%)")
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("Packet Loss Reported Max:")
                        }

                        C.Slider {
                            Layout.preferredWidth: 250
                            from: 0
                            to: 100
                            stepSize: 1
                            sendOutput: true
                            value: Chiaki.settings.packetLossMax
                            onMoved: Chiaki.settings.packetLossMax = value

                            Label {
                                anchors {
                                    left: parent.right
                                    verticalCenter: parent.verticalCenter
                                    leftMargin: 10
                                }
                                text: qsTr("%1% packet loss").arg(parent.value)
                            }
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("(5%)")
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("Show Stream Stats During Gameplay")
                        }
                        C.CheckBox {
                            lastInFocusChain: true
                            checked: Chiaki.settings.showStreamStats
                            onToggled: Chiaki.settings.showStreamStats = !Chiaki.settings.showStreamStats
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("(Unchecked)")
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
                        topMargin: 10
                    }
                    text: qsTr("Registered Consoles")
                    font.bold: true
                }

                ListView {
                    id: consolesView
                    height: 170
                    onCountChanged: {
                        consolesView.contentHeight = consolesView.count * 80 + consolesView.anchors.topMargin;
                    }
                    keyNavigationEnabled: false
                    anchors {
                        top: consolesLabel.bottom
                        horizontalCenter: consolesLabel.horizontalCenter
                        topMargin: 10
                    }
                    width: 700
                    ScrollBar.vertical: ScrollBar {
                        id: consolesScrollbar
                        policy: ScrollBar.AlwaysOn
                        visible: consolesView.contentHeight > consolesView.height
                    }
                    clip: true
                    model: Chiaki.settings.registeredHosts
                    delegate: ItemDelegate {
                        text: "%1 (%2, %3)".arg(Chiaki.settings.streamerMode ? "hidden" : modelData.mac).arg(modelData.ps5 ? "PS5" : "PS4").arg(modelData.name)
                        height: 80
                        width: parent ? parent.width : 0
                        leftPadding: autoConnectButton.width + 40

                        CheckBox {
                            property bool firstInFocusChain: false
                            property bool lastInFocusChain: false
                            property bool lastDownInFocusChain: index > consolesView.count + hiddenConsolesView.count - 2

                            id: autoConnectButton
                            anchors {
                                left: parent.left
                                verticalCenter: parent.verticalCenter
                                leftMargin: 20
                            }
                            text: qsTr("Auto-Connect")
                            checked: Chiaki.settings.autoConnectMac == modelData.mac
                            onToggled: Chiaki.settings.autoConnectMac = checked ? modelData.mac : "";

                            Keys.onPressed: (event) => {
                                switch (event.key) {
                                case Qt.Key_Right:
                                    if (!lastInFocusChain) {
                                        let item = nextItemInFocusChain();
                                        if (item)
                                            item.forceActiveFocus(Qt.TabFocusReason);
                                        event.accepted = true;
                                    }
                                    break;
                                case Qt.Key_Up:
                                    if (!firstInFocusChain) {
                                        let item = nextItemInFocusChain(false);
                                        if (item)
                                            item.forceActiveFocus(Qt.TabFocusReason);
                                        if(consolesScrollbar.position > 0.001)
                                            consolesView.flick(0, 500);
                                        let count = index > 0 ? 2: 0;
                                        for(var i = 0; i < count; i++)
                                        {
                                            let item2 = item.nextItemInFocusChain(false);
                                            if (item)
                                            {
                                                item.forceActiveFocus(Qt.TabFocusReason);
                                                item = item2;
                                            }
                                        }
                                        event.accepted = true;
                                    }
                                    break;
                                case Qt.Key_Down:
                                    if (!lastDownInFocusChain) {
                                        let item = nextItemInFocusChain();
                                        if (item)
                                            item.forceActiveFocus(Qt.TabFocusReason);
                                        if(consolesScrollbar.position < 1.0 - consolesScrollbar.size - 0.001)
                                            consolesView.flick(0, -500);
                                        let count = 2;
                                        for(var i = 0; i < count; i++)
                                        {
                                            let item2 = item.nextItemInFocusChain();
                                            if (item)
                                            {
                                                item.forceActiveFocus(Qt.TabFocusReason);
                                                item = item2;
                                            }
                                        }
                                        event.accepted = true;
                                    }
                                    break;
                                case Qt.Key_Return:
                                    if (visualFocus) {
                                        toggle();
                                        toggled();
                                    }
                                    event.accepted = true;
                                    break;
                                }
                            }
                        }

                        Button {
                            property bool firstInFocusChain: false
                            property bool lastInFocusChain: index > consolesView.count + hiddenConsolesView.count - 2
                            Material.background: visualFocus ? Material.accent : undefined

                            Component.onDestruction: {
                                if (visualFocus) {
                                    let item = nextItemInFocusChain();
                                    if (item)
                                        item.forceActiveFocus(Qt.TabFocusReason);
                                }
                            }
                            Keys.onPressed: (event) => {
                                switch (event.key) {
                                    case Qt.Key_Left:
                                        if (!firstInFocusChain) {
                                            let item = nextItemInFocusChain(false);
                                            if (item)
                                                item.forceActiveFocus(Qt.TabFocusReason);
                                            event.accepted = true;
                                        }
                                        break;
                                    case Qt.Key_Up:
                                        if (!firstInFocusChain)
                                        {
                                            let item = nextItemInFocusChain(false);
                                            if (item)
                                                item.forceActiveFocus(Qt.TabFocusReason);
                                            let count = 2;
                                            for(var i = 0; i < count; i++)
                                            {
                                                let item2 = item.nextItemInFocusChain(false);
                                                if (item)
                                                {
                                                    item.forceActiveFocus(Qt.TabFocusReason);
                                                    item = item2;
                                                }
                                            }
                                            if(consolesScrollbar.position > 0.001)
                                                consolesView.flick(0, 500);
                                            event.accepted = true;
                                        }
                                        break;
                                    case Qt.Key_Down:
                                        if (!lastInFocusChain) {
                                            let item = nextItemInFocusChain();
                                            if (item)
                                                item.forceActiveFocus(Qt.TabFocusReason);
                                            let count = index < consolesView.count - 1 ? 2: 0;
                                            for(var i = 0; i < count; i++)
                                            {
                                                let item2 = item.nextItemInFocusChain();
                                                if (item)
                                                {
                                                    item.forceActiveFocus(Qt.TabFocusReason);
                                                    item = item2;
                                                }
                                            }
                                            if(consolesScrollbar.position < 1.0 - consolesScrollbar.size - 0.001)
                                                consolesView.flick(0, -500);
                                            event.accepted = true;
                                        }
                                        break;
                                    case Qt.Key_Return:
                                        if (visualFocus) {
                                            clicked();
                                        }
                                        event.accepted = true;
                                        break;
                                }
                            }
                            anchors {
                                right: parent.right
                                verticalCenter: parent.verticalCenter
                                rightMargin: 20
                            }
                            text: qsTr("Delete")
                            onClicked: root.showConfirmDialog(qsTr("Delete Console"), qsTr("Are you sure you want to delete this console?"), () => Chiaki.settings.deleteRegisteredHost(index));
                            Material.roundedScale: Material.SmallScale
                            Material.accent: Material.Red
                        }
                    }
                }

                Label {
                    id: hiddenConsolesLabel
                    anchors {
                        top: consolesView.bottom
                        horizontalCenter: consolesView.horizontalCenter
                        topMargin: 10
                    }
                    text: qsTr("Hidden Consoles")
                    font.bold: true
                }
                ListView {
                    id: hiddenConsolesView
                    keyNavigationEnabled: false
                    height: 170
                    onCountChanged: {
                        hiddenConsolesView.contentHeight = hiddenConsolesView.count * 80 + hiddenConsolesView.anchors.topMargin;
                    }
                    anchors {
                        top: hiddenConsolesLabel.bottom
                        horizontalCenter: hiddenConsolesLabel.horizontalCenter
                        topMargin: 10
                    }
                    clip: true
                    width: 500
                    ScrollBar.vertical: ScrollBar {
                        id: hiddenConsolesScrollbar
                        policy: ScrollBar.AlwaysOn
                        visible: hiddenConsolesView.contentHeight > hiddenConsolesView.height
                    }
                    model: Chiaki.hiddenHosts
                    delegate: ItemDelegate {
                        text: "%1 (%2)".arg(Chiaki.settings.streamerMode ? "hidden" : modelData.mac).arg(modelData.name)
                        height: 80
                        width: parent ? parent.width : 0

                        Button {
                            property bool firstInFocusChain: false
                            property bool lastInFocusChain: index > hiddenConsolesView.count - 2
                            Material.background: visualFocus ? Material.accent : undefined

                            Component.onDestruction: {
                                if (visualFocus) {
                                    let item = nextItemInFocusChain();
                                    if (item)
                                        item.forceActiveFocus(Qt.TabFocusReason);
                                }
                            }
                            Keys.onPressed: (event) => {
                                switch (event.key) {
                                    case Qt.Key_Up:
                                        if (!firstInFocusChain)
                                        {
                                            let item = nextItemInFocusChain(false);
                                            if (item)
                                                item.forceActiveFocus(Qt.TabFocusReason);
                                            if(hiddenConsolesScrollbar.position > 0.001)
                                                hiddenConsolesView.flick(0, 500);
                                            event.accepted = true;
                                        }
                                        break;
                                    case Qt.Key_Down:
                                        if (!lastInFocusChain) {
                                            let item = nextItemInFocusChain();
                                            if (item)
                                                item.forceActiveFocus(Qt.TabFocusReason);
                                            if(hiddenConsolesScrollbar.position < 1.0 - hiddenConsolesScrollbar.size - 0.001)
                                                hiddenConsolesView.flick(0, -500);
                                            event.accepted = true;
                                        }
                                        break;
                                    case Qt.Key_Return:
                                        if (visualFocus) {
                                            clicked();
                                        }
                                        event.accepted = true;
                                        break;
                                }
                            }
                            anchors {
                                right: parent.right
                                verticalCenter: parent.verticalCenter
                                rightMargin: 20
                            }
                            text: qsTr("Unhide")
                            onClicked: root.showConfirmDialog(qsTr("Unhide Console"), qsTr("Are you sure you want to unhide this console?"), () => Chiaki.unhideHost(modelData.mac));
                            Material.roundedScale: Material.SmallScale
                            Material.accent: Material.Red
                        }
                    }
                }
            }

            Item {
                // Keys
                id: controllerMapping
                GridLayout {
                    anchors {
                        top: parent.top
                        horizontalCenter: parent.horizontalCenter
                        topMargin: 20
                    }
                    columns: 3
                    rowSpacing: 3
                    columnSpacing: 10

                    Button {
                        text: "Reset All Keys"
                        Layout.alignment: Qt.AlignRight
                        property bool firstInFocusChain: true
                        property bool lastInFocusChain: false
                        onClicked: Chiaki.settings.clearKeyMapping()
                        Material.roundedScale: Material.SmallScale
                        Material.background: visualFocus ? Material.accent : undefined

                        Component.onDestruction: {
                            if (visualFocus) {
                                let item = nextItemInFocusChain();
                                if (item)
                                    item.forceActiveFocus(Qt.TabFocusReason);
                            }
                        }
                        Keys.onPressed: (event) => {
                            switch (event.key) {
                            case Qt.Key_Down:
                                if (!lastInFocusChain) {
                                    let item = nextItemInFocusChain();
                                    if (item)
                                        item.forceActiveFocus(Qt.TabFocusReason);
                                    for(var i = 0; i < 3; i++)
                                    {
                                        let item2 = item.nextItemInFocusChain();
                                        if (item)
                                        {
                                            item.forceActiveFocus(Qt.TabFocusReason);
                                            item = item2;
                                        }
                                    }
                                    event.accepted = true;
                                }
                                break;
                            case Qt.Key_Right:
                                if (!lastInFocusChain) {
                                    let item = nextItemInFocusChain();
                                    if (item)
                                        item.forceActiveFocus(Qt.TabFocusReason);
                                    event.accepted = true;
                                }
                                break;
                            case Qt.Key_Return:
                                if (visualFocus) {
                                    clicked();
                                }
                                event.accepted = true;
                                break;
                            }
                        }
                    }

                    CheckBox {
                        text: qsTr("Enable Keyboard mapping")
                        checked: {
                            Chiaki.settings.keyboardEnabled
                        }
                        onToggled: Chiaki.settings.keyboardEnabled = checked
                        Layout.alignment: Qt.AlignRight
                        property bool firstInFocusChain: false
                        property bool lastInFocusChain: false
                        Material.roundedScale: Material.SmallScale
                        Material.background: visualFocus ? Material.accent : undefined

                        Component.onDestruction: {
                            if (visualFocus) {
                                let item = nextItemInFocusChain();
                                if (item)
                                    item.forceActiveFocus(Qt.TabFocusReason);
                            }
                        }
                        Keys.onPressed: (event) => {
                            switch (event.key) {
                                case Qt.Key_Left:
                                    if (!firstInFocusChain) {
                                        let item = nextItemInFocusChain(false);
                                        if (item)
                                            item.forceActiveFocus(Qt.TabFocusReason);
                                        event.accepted = true;
                                    }
                                    break;
                                case Qt.Key_Right:
                                    if  (!lastInFocusChain) {
                                        let item = nextItemInFocusChain();
                                        if (item)
                                            item.forceActiveFocus(Qt.TabFocusReason);
                                        event.accepted = true;
                                    }
                                    break;
                                case Qt.Key_Down:
                                    if (!lastInFocusChain) {
                                        let item = nextItemInFocusChain();
                                        if (item)
                                            item.forceActiveFocus(Qt.TabFocusReason);
                                        for(var i = 0; i < 3; i++)
                                        {
                                            let item2 = item.nextItemInFocusChain();
                                            if (item)
                                            {
                                                item.forceActiveFocus(Qt.TabFocusReason);
                                                item = item2;
                                            }
                                        }
                                        event.accepted = true;
                                    }
                                    break;
                                case Qt.Key_Return:
                                    if (visualFocus) {
                                        toggle();
                                        toggled();
                                    }
                                    event.accepted = true;
                                    break;
                            }
                        }
                    }
                    CheckBox {
                        text: qsTr("Enable Mouse Touchpad")
                        checked: {
                            Chiaki.settings.mouseTouchEnabled
                        }
                        onToggled: Chiaki.settings.mouseTouchEnabled = checked
                        Layout.alignment: Qt.AlignRight
                        property bool firstInFocusChain: false
                        property bool lastInFocusChain: false
                        Material.roundedScale: Material.SmallScale
                        Material.background: visualFocus ? Material.accent : undefined

                        Component.onDestruction: {
                            if (visualFocus) {
                                let item = nextItemInFocusChain();
                                if (item)
                                    item.forceActiveFocus(Qt.TabFocusReason);
                            }
                        }
                        Keys.onPressed: (event) => {
                            switch (event.key) {
                                case Qt.Key_Left:
                                    if (!firstInFocusChain) {
                                        let item = nextItemInFocusChain(false);
                                        if (item)
                                            item.forceActiveFocus(Qt.TabFocusReason);
                                        event.accepted = true;
                                    }
                                    break;
                                case Qt.Key_Down:
                                    if (!lastInFocusChain) {
                                        let item = nextItemInFocusChain();
                                        if (item)
                                            item.forceActiveFocus(Qt.TabFocusReason);
                                        for(var i = 0; i < 3; i++)
                                        {
                                            let item2 = item.nextItemInFocusChain();
                                            if (item)
                                            {
                                                item.forceActiveFocus(Qt.TabFocusReason);
                                                item = item2;
                                            }
                                        }
                                        event.accepted = true;
                                    }
                                    break;
                                case Qt.Key_Return:
                                    if (visualFocus) {
                                        toggle();
                                        toggled();
                                    }
                                    event.accepted = true;
                                    break;
                            }
                        }
                    }
                    Repeater {
                        id: chiakiKeys
                        model: Chiaki.settings.controllerMapping

                        RowLayout {
                            spacing: 20

                            Label {
                                Layout.preferredWidth: 200
                                horizontalAlignment: Text.AlignRight
                                text: modelData.buttonName
                            }

                            Button {
                                property bool firstInFocusChain: false
                                property bool lastInFocusChain: index == (chiakiKeys.count - 1)
                                Layout.preferredWidth: 170
                                Layout.preferredHeight: 52
                                text: modelData.keyName
                                Material.roundedScale: Material.SmallScale
                                Material.background: visualFocus ? Material.accent : undefined
                                Component.onDestruction: {
                                    if (visualFocus) {
                                        let item = nextItemInFocusChain();
                                        if (item)
                                            item.forceActiveFocus(Qt.TabFocusReason);
                                    }
                                }
                                onClicked: {
                                    keyDialog.show({
                                        value: modelData.buttonValue,
                                        mappingIndex: index,
                                        callback: (name) => text = name,
                                    });
                                }
                                Keys.onPressed: (event) => {
                                    switch (event.key) {
                                        case Qt.Key_Left:
                                            if (!firstInFocusChain && ((index % 3) != 0)) {
                                                let item = nextItemInFocusChain(false);
                                                if (item)
                                                    item.forceActiveFocus(Qt.TabFocusReason);
                                                event.accepted = true;
                                            }
                                            break;
                                        case Qt.Key_Right:
                                            if  (!lastInFocusChain && (index % 3) != 2) {
                                                let item = nextItemInFocusChain();
                                                if (item)
                                                    item.forceActiveFocus(Qt.TabFocusReason);
                                                event.accepted = true;
                                            }
                                            break;
                                        case Qt.Key_Up:
                                            if (!firstInFocusChain)
                                            {
                                                let item = nextItemInFocusChain(false);
                                                if (item)
                                                    item.forceActiveFocus(Qt.TabFocusReason);
                                                for(var i = 0; i < 3; i++)
                                                {
                                                    let item2 = item.nextItemInFocusChain(false);
                                                    if (item)
                                                    {
                                                        item.forceActiveFocus(Qt.TabFocusReason);
                                                        item = item2;
                                                    }
                                                }
                                                event.accepted = true;
                                            }
                                            break;
                                        case Qt.Key_Down:
                                            if (!lastInFocusChain && index < (chiakiKeys.count - 3)) {
                                                let item = nextItemInFocusChain();
                                                if (item)
                                                    item.forceActiveFocus(Qt.TabFocusReason);
                                                for(var i = 0; i < 3; i++)
                                                {
                                                    let item2 = item.nextItemInFocusChain();
                                                    if (item)
                                                    {
                                                        item.forceActiveFocus(Qt.TabFocusReason);
                                                        item = item2;
                                                    }
                                                }
                                                event.accepted = true;
                                            }
                                            break;
                                        case Qt.Key_Return:
                                            if (visualFocus) {
                                                clicked();
                                            }
                                            break;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Item {
                // Controllers
                Flickable {
                    id: controllersFlick
                    implicitWidth: parent.width ? parent.width: 0
                    implicitHeight: parent.height ? parent.height: 0
                    anchors {
                        fill: parent
                        topMargin: 20
                        bottomMargin: 20
                        leftMargin: parent.width ? (parent.width / 2 - controllersLayout.width / 2) : 0
                    }
                    clip: true
                    contentWidth: controllersLayout.width
                    contentHeight: controllersLayout.height
                    flickableDirection: Flickable.AutoFlickIfNeeded
                    ScrollBar.vertical: ScrollBar {
                        id: controllersScrollbar
                        policy: ScrollBar.AlwaysOn
                        visible: controllersFlick.contentHeight > controllersFlick.implicitHeight - controllersFlick.anchors.topMargin - controllersFlick.anchors.bottomMargin
                    }
                    ColumnLayout {
                        id: controllersLayout
                        anchors {
                            top: parent.top
                            horizontalCenter: parent.horizontalCenter
                        }
                        spacing: 10
                        C.Button {
                            sendOutput: true
                            Layout.alignment: Qt.AlignHCenter
                            id: controllerMappingChange
                            firstInFocusChain: true
                            text: "Change Controller Mapping"
                            onClicked: controllerMappingDialog.show({
                                reset: false
                            });
                        }
                        C.Button {
                            sendOutput: true
                            Layout.alignment: Qt.AlignHCenter
                            id: controllerMappingReset
                            text: "Reset Controller Mapping"
                            onClicked: controllerMappingDialog.show({
                                reset: true
                            });
                        }

                        RowLayout {
                            spacing: 10
                            Layout.alignment: Qt.AlignHCenter
                            Label {
                                Layout.alignment: Qt.AlignRight
                                text: qsTr("Background Controller Events:")
                            }
                            C.CheckBox {
                                id: backgroundController
                                sendOutput: true
                                text: qsTr("Process controller input when application is in background")
                                checked: {
                                    Chiaki.settings.allowJoystickBackgroundEvents
                                }
                                onToggled: Chiaki.settings.allowJoystickBackgroundEvents = checked
                            }

                            Label {
                                Layout.alignment: Qt.AlignRight
                                text: qsTr("(Checked)")
                            }
                        }
                        RowLayout {
                            spacing: 10
                            Layout.alignment: Qt.AlignHCenter
                            Label {
                                Layout.alignment: Qt.AlignRight
                                text: qsTr("Dpad Touchpad Emulation")
                            }
                            C.CheckBox {
                                sendOutput: true
                                id: dpadTouch
                                checked: Chiaki.settings.dpadTouchEnabled
                                onToggled: Chiaki.settings.dpadTouchEnabled = !Chiaki.settings.dpadTouchEnabled
                                KeyNavigation.priority: KeyNavigation.BeforeItem
                                KeyNavigation.up: backgroundController
                                KeyNavigation.left: dpadTouch
                                KeyNavigation.right: dpadTouch
                                KeyNavigation.down: {
                                    if(touchIncrement.visible)
                                        touchIncrement
                                    else
                                        posButtons
                                }
                            }

                            Label {
                                Layout.alignment: Qt.AlignRight
                                text: qsTr("(Checked)")
                            }
                        }

                        RowLayout {
                            id: touchIncrementLayout
                            spacing: 10
                            Layout.alignment: Qt.AlignHCenter
                            visible: Chiaki.settings.dpadTouchEnabled
                            Label {
                                Layout.alignment: Qt.AlignRight
                                text: qsTr("Dpad Touch Increment:")
                            }

                            C.Slider {
                                sendOutput: true
                                id: touchIncrement
                                Layout.preferredWidth: 250
                                from: 1
                                to: 1079
                                stepSize: 1
                                value: Chiaki.settings.dpadTouchIncrement
                                onMoved: Chiaki.settings.dpadTouchIncrement = value

                                Label {
                                    anchors {
                                        left: parent.right
                                        verticalCenter: parent.verticalCenter
                                        leftMargin: 10
                                    }
                                    text: qsTr("%1 mm").arg(parent.value / 100)
                                }
                                KeyNavigation.priority: KeyNavigation.BeforeItem
                                KeyNavigation.up: dpadTouch
                                KeyNavigation.down: {
                                    if(dpadShortcut1.visible)
                                        dpadShortcut1
                                    else
                                        posButtons;
                                }
                            }

                            Label {
                                Layout.alignment: Qt.AlignRight
                                Layout.leftMargin: 100
                                text: qsTr("(0.3 mm)")
                            }
                        }
                        RowLayout {
                            spacing: 10
                            visible: Chiaki.settings.dpadTouchEnabled
                            Layout.alignment: Qt.AlignHCenter
                            Label {
                                Layout.alignment: Qt.AlignRight
                                text: qsTr("Dpad Regular/Touch Combo:")
                            }

                            C.ComboBox {
                                id: dpadShortcut1
                                implicitContentWidthPolicy: ComboBox.WidestText
                                firstInFocusChain: false
                                model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                                currentIndex: Chiaki.settings.dpadTouchShortcut1
                                onActivated: index => Chiaki.settings.dpadTouchShortcut1 = index
                                KeyNavigation.priority: {
                                    if(!popup.visible)
                                        KeyNavigation.BeforeItem
                                    else
                                        KeyNavigation.AfterItem
                                }
                                KeyNavigation.up: touchIncrement
                                KeyNavigation.down: posButtons
                                KeyNavigation.left: dpadShortcut1
                                KeyNavigation.right: dpadShortcut2
                            }

                            C.ComboBox {
                                id: dpadShortcut2
                                implicitContentWidthPolicy: ComboBox.WidestText
                                firstInFocusChain: false
                                model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                                currentIndex: Chiaki.settings.dpadTouchShortcut2
                                onActivated: index => Chiaki.settings.dpadTouchShortcut2 = index
                                KeyNavigation.priority: {
                                    if(!popup.visible)
                                        KeyNavigation.BeforeItem
                                    else
                                        KeyNavigation.AfterItem
                                }
                                KeyNavigation.up: touchIncrement
                                KeyNavigation.down: posButtons
                                KeyNavigation.left: dpadShortcut1
                                KeyNavigation.right: dpadShortcut3
                            }

                            C.ComboBox {
                                id: dpadShortcut3
                                implicitContentWidthPolicy: ComboBox.WidestText
                                firstInFocusChain: false
                                model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                                currentIndex: Chiaki.settings.dpadTouchShortcut3
                                onActivated: index => Chiaki.settings.dpadTouchShortcut3 = index
                                KeyNavigation.priority: {
                                    if(!popup.visible)
                                        KeyNavigation.BeforeItem
                                    else
                                        KeyNavigation.AfterItem
                                }
                                KeyNavigation.up: touchIncrement
                                KeyNavigation.down: posButtons
                                KeyNavigation.left: dpadShortcut2
                                KeyNavigation.right: dpadShortcut4
                            }

                            C.ComboBox {
                                id: dpadShortcut4
                                implicitContentWidthPolicy: ComboBox.WidestText
                                firstInFocusChain: false
                                model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                                currentIndex: Chiaki.settings.dpadTouchShortcut4
                                onActivated: index => Chiaki.settings.dpadTouchShortcut4 = index
                                KeyNavigation.priority: {
                                    if(!popup.visible)
                                        KeyNavigation.BeforeItem
                                    else
                                        KeyNavigation.AfterItem
                                }
                                KeyNavigation.up: touchIncrement
                                KeyNavigation.down: posButtons
                                KeyNavigation.left: dpadShortcut3
                                KeyNavigation.right: dpadShortcut4
                            }

                            Label {
                                Layout.alignment: Qt.AlignRight
                                text: qsTr("(L1+R1+dpad Up)")
                            }
                        }
                        RowLayout {
                            spacing: 10
                            Layout.alignment: Qt.AlignHCenter
                            Label {
                                Layout.alignment: Qt.AlignRight
                                text: qsTr("Buttons By Position:")
                            }

                            C.CheckBox {
                                id: posButtons
                                sendOutput: true
                                text: qsTr("Use buttons by position instead of by label")
                                checked: Chiaki.settings.buttonsByPosition
                                onToggled: Chiaki.settings.buttonsByPosition = checked
                                KeyNavigation.priority: KeyNavigation.BeforeItem
                                KeyNavigation.up: {
                                    if(dpadShortcut1.visible)
                                        dpadShortcut1
                                    else
                                        dpadTouch;
                                }
                                KeyNavigation.down: rumbleHaptics
                                KeyNavigation.left: posButtons
                                KeyNavigation.right: posButtons
                            }

                            Label {
                                Layout.alignment: Qt.AlignRight
                                text: qsTr("(Unchecked)")
                            }
                        }
                        RowLayout {
                            spacing: 10
                            Layout.alignment: Qt.AlignHCenter
                            Label {
                                Layout.alignment: Qt.AlignRight
                                text: qsTr("Rumble Haptics:")
                            }

                            C.ComboBox {
                                id: rumbleHaptics
                                Layout.preferredWidth: 400
                                model: [qsTr("Off"), qsTr("Very Weak"), qsTr("Weak"), qsTr("Normal"), qsTr("Strong"), qsTr("Very Strong")]
                                currentIndex: Chiaki.settings.rumbleHapticsIntensity
                                onActivated: (index) => Chiaki.settings.rumbleHapticsIntensity = index;
                            }

                            Label {
                                Layout.alignment: Qt.AlignRight
                                text: qsTr("(Normal)")
                            }
                        }
                        RowLayout {
                            spacing: 10
                            Layout.alignment: Qt.AlignHCenter
                            Label {
                                Layout.alignment: Qt.AlignRight
                                text: qsTr("True Haptics Intensity:")
                            }

                            C.Slider {
                                sendOutput: true
                                id: hapticOverride
                                Layout.preferredWidth: 250
                                from: 0
                                to: 2
                                stepSize: 0.1
                                value: Chiaki.settings.hapticOverride
                                onMoved: Chiaki.settings.hapticOverride = value;
                                lastInFocusChain: true
                                Label {
                                    anchors {
                                        left: parent.right
                                        verticalCenter: parent.verticalCenter
                                        leftMargin: 10
                                    }
                                    text: {
                                        if(parent.value > 0.99 && parent.value < 1.01)
                                            qsTr("console setting")
                                        else
                                            (parent.value * 100).toFixed(0) + qsTr(" % console setting")
                                    }
                                }
                            }

                            Label {
                                Layout.alignment: Qt.AlignRight
                                Layout.leftMargin: 250
                                text: qsTr("(console setting)")
                            }
                        }
                    }
                }
            }

            Item {
                // Config (PSN Remote Connection Setup and Import/Export)
                GridLayout {
                    anchors {
                        top: parent.top
                        horizontalCenter: parent.horizontalCenter
                        topMargin: 50
                    }
                    columns: 1
                    rowSpacing: 20
                    columnSpacing: 10

                    Label {
                        text: {
                            if(Chiaki.settings.currentProfile)
                                qsTr("Current Profile: ") + Chiaki.settings.currentProfile
                            else
                                qsTr("Current Profile: default")
                        }
                    }

                    C.Button {
                        id: profile
                        firstInFocusChain: true
                        text: qsTr("Manage Profiles")
                        onClicked: {
                            root.showProfileDialog()
                        }
                        Material.roundedScale: Material.SmallScale
                    }

                    C.Button {
                        id: openPsnLogin
                        text: qsTr("Login to PSN")
                        onClicked: {
                            root.showPSNTokenDialog(false)
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
                        text: qsTr("Clear PSN Token")
                        onClicked: {
                            Chiaki.settings.psnRefreshToken = ""
                            Chiaki.settings.psnAuthToken = ""
                            Chiaki.settings.psnAuthTokenExpiry = ""
                            Chiaki.settings.psnAccountId = ""
                            openPsnLogin.forceActiveFocus(Qt.TabFocusReason);
                        }
                        Material.roundedScale: Material.SmallScale
                        visible: Chiaki.settings.psnRefreshToken && Chiaki.settings.psnAuthToken && Chiaki.settings.psnAuthTokenExpiry && Chiaki.settings.psnAccountId
                    }

                    C.Button {
                        id: exportButton
                        text: qsTr("Export settings to file")
                        onClicked: {
                            Chiaki.settings.exportSettings();
                        }
                        Material.roundedScale: Material.SmallScale
                    }

                    C.Button {
                        id: importButton
                        text: qsTr("Import settings from file")
                        onClicked: {
                            Chiaki.settings.importSettings();
                        }
                        Material.roundedScale: Material.SmallScale
                    }

                    C.Button {
                        id: aboutButton
                        text: qsTr("About %1-ng").arg(Qt.application.name)
                        onClicked: aboutDialog.open()
                        Material.roundedScale: Material.SmallScale
                    }

                    C.CheckBox {
                        text: qsTr("Verbose Logging (unchecked)")
                        checked: Chiaki.settings.logVerbose
                        lastInFocusChain: true
                        onToggled: Chiaki.settings.logVerbose = checked
                    }
                }
            }
        }

        Item {
            Timer {
                id: openTimer
                interval: 100
                running: false
                onTriggered: {
                    if(controllerMappingDialog.resetMapping)
                    {
                        if(!Chiaki.controllerMappingDefaultMapping)
                        {
                            quitControllerMapping = false;
                            Chiaki.controllerMappingReset();
                        }
                        controllerMappingDialog.close();
                    }
                    else
                    {
                        controllerMappingChange.forceActiveFocus(Qt.TabFocusReason);
                        root.showControllerMappingDialog();
                        quitControllerMapping = false;
                        controllerMappingDialog.resetFocus = false;
                        controllerMappingDialog.close();
                    }
                }
            }
        }

        Dialog {
            id: aboutDialog
            parent: Overlay.overlay
            x: Math.round((root.width - width) / 2)
            y: Math.round((root.height - height) / 2)
            title: qsTr("About %1-ng").arg(Qt.application.name)
            modal: true
            standardButtons: Dialog.Ok
            Material.roundedScale: Material.MediumScale
            onAboutToHide: aboutButton.forceActiveFocus(Qt.TabFocusReason)

            RowLayout {
                spacing: 50
                onVisibleChanged: if (visible) forceActiveFocus(Qt.TabFocusReason)
                Keys.onReturnPressed: aboutDialog.close()
                Keys.onEscapePressed: aboutDialog.close()

                Image {
                    Layout.preferredWidth: 200
                    fillMode: Image.PreserveAspectFit
                    verticalAlignment: Image.AlignTop
                    source: "qrc:icons/chiaking-logo.svg"
                }

                Label {
                    Layout.preferredWidth: 400
                    verticalAlignment: Text.AlignTop
                    wrapMode: Text.Wrap
                    text: "<h1>chiaki-ng</h1> by Street Pea, version %1
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
            focus: false
            property int buttonValue
            property var buttonCallback
            property var keysIndex
            parent: Overlay.overlay
            x: Math.round((root.width - width) / 2)
            y: Math.round((root.height - height) / 2)
            title: qsTr("Key Capture")
            modal: true
            standardButtons: Dialog.Close
            closePolicy: Popup.CloseOnPressOutside
            onOpened: keyLabel.forceActiveFocus(Qt.TabFocusReason)
            onClosed: {
                let item = chiakiKeys.itemAt(keysIndex)
                if(item)
                {
                    let item2 = item.children[1];
                    if(item2)
                        item2.forceActiveFocus(Qt.TabFocusReason);
                }
                keyLabel.focus = false;
                focus = false;
            }
            Material.roundedScale: Material.MediumScale

            function show(opts) {
                buttonValue = opts.value;
                buttonCallback = opts.callback;
                keysIndex = opts.mappingIndex;
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

        Dialog {
            id: controllerMappingDialog
            property bool resetFocus: true
            property bool resetMapping: false
            parent: Overlay.overlay
            x: Math.round((root.width - width) / 2)
            y: Math.round((root.height - height) / 2)
            title: qsTr("Controller Capture")
            modal: true
            standardButtons: Dialog.Close
            closePolicy: Popup.CloseOnPressOutside
            onOpened: {
                controllerLabel.forceActiveFocus(Qt.TabFocusReason);
                Chiaki.creatingControllerMapping(true);
            }
            onClosed: {
                if(resetFocus)
                {
                    if(resetMapping)
                        controllerMappingReset.forceActiveFocus(Qt.TabFocusReason);
                    else
                        controllerMappingChange.forceActiveFocus(Qt.TabFocusReason);
                    focus = false;
                }
                else
                {
                    resetFocus = true;
                    focus = false;
                }
                if(quitControllerMapping)
                    Chiaki.controllerMappingQuit();
                else
                    quitControllerMapping = true;
            }
            Material.roundedScale: Material.MediumScale

            function show(opts) {
                resetMapping = opts.reset;
                open();
            }
            Label {
                id: controllerLabel
                text: qsTr("Choose the controller by pressing any button on the controller")
            }
        }

        Dialog {
            id: steamControllerMappingDialog
            property bool resetMapping: false
            parent: Overlay.overlay
            x: Math.round((root.width - width) / 2)
            y: Math.round((root.height - height) / 2)
            title: qsTr("Controller Managed by Steam")
            modal: true
            standardButtons: Dialog.Close
            closePolicy: Popup.NoAutoClose
            onOpened: {
                steamLabel.forceActiveFocus(Qt.TabFocusReason);
            }
            onClosed: {
                if(resetMapping)
                    controllerMappingReset.forceActiveFocus(Qt.TabFocusReason);
                else
                    controllerMappingChange.forceActiveFocus(Qt.TabFocusReason);
                focus = false;
            }
            Material.roundedScale: Material.MediumScale

            Label {
                id: steamLabel
                wrapMode: TextEdit.Wrap
                text: qsTr("This controller is managed by Steam.\nPlease use Steam to map controller or disable Steam Input for the controller before mapping here.")
                Keys.onReturnPressed: steamControllerMappingDialog.close();
                Keys.onEscapePressed: steamControllerMappingDialog.close();
            }
        }

        Connections {
            target: Chiaki

            function onControllerMappingInProgressChanged()
            {
                if(Chiaki.controllerMappingInProgress)
                    openTimer.start();
            }

            function onControllerMappingSteamControllerSelected()
            {
                controllerMappingDialog.resetFocus = false;
                quitControllerMapping = false;
                steamControllerMappingDialog.resetMapping = controllerMappingDialog.resetMapping;
                controllerMappingDialog.close();
                steamControllerMappingDialog.open();
            }
        }
    }
}
