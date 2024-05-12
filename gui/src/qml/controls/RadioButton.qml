import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

RadioButton {
    property bool firstInFocusChain: false
    property bool lastInFocusChain: false

    Keys.onPressed: (event) => {
        switch (event.key) {
        case Qt.Key_Up:
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
