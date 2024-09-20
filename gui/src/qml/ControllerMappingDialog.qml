import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

import "controls" as C

DialogView {
    id: dialog
    property bool finalClosing: false
    property bool quitButtonMapping: true
    title: Chiaki.currentControllerType
    buttonText: qsTr("✓ Update Controller Mapping")
    buttonEnabled: Chiaki.controllerMappingAltered
    onAccepted: {
        Chiaki.controllerMappingApply();
        Chiaki.controllerMappingQuit();
    }
    Component.onDestruction: {
        if(Chiaki.controllerMappingInProgress)
        {
            finalClosing = true;
            Chiaki.controllerMappingButtonQuit();
            Chiaki.controllerMappingQuit();
        }  
    }

    Item {
        // Controller Mapping
        GridLayout {
            anchors {
                top: parent.top
                horizontalCenter: parent.horizontalCenter
                topMargin: 20
            }
            columns: 3
            rowSpacing: 10
            columnSpacing: 10

        CheckBox {
            id: analogStickMapping
            property bool firstInFocusChain: true
            property bool lastInFocusChain: false
            text: qsTr("Allow mapping analog sticks' x/y-axis")
            checked: Chiaki.enableAnalogStickMapping
            onToggled: Chiaki.enableAnalogStickMapping = checked
            Keys.onPressed: (event) => {
                switch (event.key) {
                case Qt.Key_Down:
                    if (!lastInFocusChain) {
                        let item = nextItemInFocusChain();
                        if (item)
                            item.forceActiveFocus(Qt.TabFocusReason);
                        for(var i = 0; i < 5; i++)
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
                        toggle();
                        toggled();
                    }
                    event.accepted = true;
                    break;
                }
            }
        }

            Repeater {
                activeFocusOnTab: true
                id: chiakiButtons
                model: Chiaki.currentControllerMapping
                RowLayout {
                    spacing: 20

                    Label {
                        Layout.preferredWidth: 150
                        horizontalAlignment: Text.AlignRight
                        text: modelData.buttonName
                    }

                    Button {
                        property bool firstInFocusChain: false
                        property bool lastInFocusChain: false
                        Layout.preferredWidth: 90
                        Layout.preferredHeight: 52
                        text: {
                            if(modelData.physicalButton.length > 0)
                                modelData.physicalButton[0]
                        }
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
                                    if (!firstInFocusChain && (((index + 1)% 3) != 0)) {
                                        let item = nextItemInFocusChain(false);
                                        if (item)
                                            item.forceActiveFocus(Qt.TabFocusReason);
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
                                case Qt.Key_Up:
                                    if (!firstInFocusChain && index > 1)
                                    {
                                        let item = nextItemInFocusChain(false);
                                        if (item)
                                            item.forceActiveFocus(Qt.TabFocusReason);
                                        let count = index == 2 ? 5 : 6;
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
                                    if (!lastInFocusChain && index < (chiakiButtons.count - 3)) {
                                        let item = nextItemInFocusChain();
                                        if (item)
                                            item.forceActiveFocus(Qt.TabFocusReason);
                                        for(var i = 0; i < 6; i++)
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
                                    event.accepted = true;
                                    break;
                            }
                        }
                        Material.roundedScale: Material.SmallScale
                        onClicked: {
                            buttonDialog.show({
                                value: modelData.buttonValue,
                                buttonIndex: 0,
                                mappingIndex: index,
                            });
                        }
                    }

                    Button {
                        property bool firstInFocusChain: false
                        property bool lastInFocusChain: index == (chiakiButtons.count - 1)
                        Layout.preferredWidth: 90
                        Layout.preferredHeight: 52
                        text: {
                            if(modelData.physicalButton.length > 1)
                                modelData.physicalButton[1]
                        }
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
                                    if  (!lastInFocusChain && ((index - 1) % 3) != 0) {
                                        let item = nextItemInFocusChain();
                                        if (item)
                                            item.forceActiveFocus(Qt.TabFocusReason);
                                        event.accepted = true;
                                    }
                                    break;
                                case Qt.Key_Up:
                                    if (!firstInFocusChain && index > 1) {
                                        let item = nextItemInFocusChain(false);
                                        if (item)
                                            item.forceActiveFocus(Qt.TabFocusReason);
                                        for(var i = 0; i < 6; i++)
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
                                    if (!lastInFocusChain && index < (chiakiButtons.count - 3)) {
                                        let item = nextItemInFocusChain();
                                        if (item)
                                            item.forceActiveFocus(Qt.TabFocusReason);
                                        for(var i = 0; i < 6; i++)
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
                                    event.accepted = true;
                                    break;
                            }
                        }
                        Material.roundedScale: Material.SmallScale
                        onClicked: {
                            buttonDialog.show({
                                value: modelData.buttonValue,
                                buttonIndex: 1,
                                mappingIndex: index,
                            });
                        }
                    }
                }
            }
        }

        Dialog {
            property int buttonValue
            property int buttonIndex
            property int mappingIndex
            property bool resetFocus: true
            id: buttonDialog
            parent: Overlay.overlay
            x: Math.round((root.width - width) / 2)
            y: Math.round((root.height - height) / 2)
            title: qsTr("Button Capture")
            modal: true
            standardButtons: Dialog.Close
            closePolicy: Popup.CloseOnPressOutside
            onOpened: {
                Chiaki.controllerMappingSelectButton();
                buttonLabel.forceActiveFocus((Qt.TabFocusReason));
            }
            onClosed: {
                if(quitButtonMapping)
                    Chiaki.controllerMappingButtonQuit();
                else
                    quitButtonMapping = true;
                if(resetFocus)
                {
                    let item = chiakiButtons.itemAt(mappingIndex)
                    if(item)
                    {
                        let item2 = item.children[buttonIndex + 1];
                        if(item2)
                            item2.forceActiveFocus(Qt.TabFocusReason);
                    }
                }
                else
                    resetFocus = true;
                buttonLabel.focus = false;
                focus = false;
            }
            Material.roundedScale: Material.MediumScale

            function show(opts) {
                buttonValue = opts.value;
                buttonIndex = opts.buttonIndex;
                mappingIndex = opts.mappingIndex
                open();
            }

            Label {
                Layout.preferredWidth: 400
                id: buttonLabel
                text: qsTr("Press any ") + Chiaki.currentControllerType + qsTr(" button to map to DualSense controller button or click close")
            }
        }

        Dialog {
            property int buttonValue
            property var buttons
            property var oldButtonName
            property int buttonIndex
            property int mappingIndex
            id: chooseButtonDialog
            parent: Overlay.overlay
            x: Math.round((root.width - width) / 2)
            y: Math.round((root.height - height) / 2)
            title: qsTr("Choose Button")
            modal: true
            standardButtons: Dialog.Close
            onOpened: {
                quitButtonMapping = false;
                button0.forceActiveFocus((Qt.TabFocusReason));
            }
            onClosed: {
                if(quitButtonMapping)
                    Chiaki.controllerMappingButtonQuit();
                else
                    quitButtonMapping = true;
                let item = chiakiButtons.itemAt(mappingIndex)
                if(item)
                {
                    let item2 = item.children[buttonIndex + 1];
                    if(item2)
                        item2.forceActiveFocus(Qt.TabFocusReason);
                }
                focus = false;
            }
            Material.roundedScale: Material.MediumScale

            function show(opts) {
                buttons = opts.buttonList;
                oldButtonName = opts.oldChiakiButtonName;
                buttonValue = opts.buttonValue;
                buttonIndex = opts.buttonIndex;
                mappingIndex = opts.mappingIndex;
                open();
            }

            ColumnLayout {
                Label {
                    id: chooseButtonLabel
                    Layout.preferredWidth: 400
                    text: qsTr("Choose ") + qsTr(" between the 2 physical buttons currently mapped to " + chooseButtonDialog.oldButtonName)
                }

                RowLayout {
                    Layout.topMargin: 30
                    Layout.alignment: Qt.AlignCenter
                    spacing: 30

                    C.Button {
                        id: button0
                        firstInFocusChain: true
                        Layout.preferredWidth: 170
                        Layout.preferredHeight: 52
                        text: {
                            if(typeof chooseButtonDialog.buttons !== "undefined")
                                chooseButtonDialog.buttons[0]
                        }
                        Material.roundedScale: Material.MediumScale
                        KeyNavigation.priority: KeyNavigation.BeforeItem
                        KeyNavigation.right: button1
                        onClicked: {
                            Chiaki.updateButton(chooseButtonDialog.buttonValue, chooseButtonDialog.buttons[0], chooseButtonDialog.buttonIndex);
                            quitButtonMapping = false;
                            chooseButtonDialog.close();
                        }
                    }

                    C.Button {
                        id: button1
                        lastInFocusChain: true
                        Layout.preferredWidth: 170
                        Layout.preferredHeight: 52
                        text: {
                            if(typeof chooseButtonDialog.buttons !== "undefined")
                                chooseButtonDialog.buttons[1]
                        }
                        Material.roundedScale: Material.MediumScale
                        KeyNavigation.priority: KeyNavigation.BeforeItem
                        KeyNavigation.left: button0
                        onClicked: {
                            Chiaki.updateButton(chooseButtonDialog.buttonValue, chooseButtonDialog.buttons[1], chooseButtonDialog.buttonIndex);
                            quitButtonMapping = false;
                            chooseButtonDialog.close();
                        }
                    }
                }
            }
        }

        Connections {
            target: Chiaki

            function onControllerMappingInProgressChanged()
            {
                if(!Chiaki.controllerMappingInProgress && !finalClosing)
                    dialog.close();
            }

            function onControllerMappingButtonSelected(original_button_map, chiaki_button_value, chiaki_button_name)
            {
                if(original_button_map.length > 1)
                {
                    buttonDialog.resetFocus = false;
                    buttonDialog.close();
                    chooseButtonDialog.show({
                        buttonList: original_button_map,
                        oldChiakiButton: chiaki_button_value,
                        oldChiakiButtonName: chiaki_button_name,
                        buttonValue: buttonDialog.buttonValue,
                        buttonIndex: buttonDialog.buttonIndex,
                        mappingIndex: buttonDialog.mappingIndex,
                    });
                }
                else
                {
                    Chiaki.updateButton(buttonDialog.buttonValue, original_button_map[0], buttonDialog.buttonIndex);
                    quitButtonMapping = false;
                    buttonDialog.close();
                }

            }
        }
    }
}
