#include "qmlcontroller.h"

#include <QTimer>
#include <QKeyEvent>
#include <QStyleHints>
#include <QGuiApplication>

static QVector<QPair<uint32_t, Qt::Key>> key_map = {
    { CHIAKI_CONTROLLER_BUTTON_DPAD_UP, Qt::Key_Up },
    { CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN, Qt::Key_Down },
    { CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT, Qt::Key_Left },
    { CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT, Qt::Key_Right },
    { CHIAKI_CONTROLLER_BUTTON_CROSS, Qt::Key_Return },
    { CHIAKI_CONTROLLER_BUTTON_MOON, Qt::Key_Escape },
    { CHIAKI_CONTROLLER_BUTTON_BOX, Qt::Key_No },
    { CHIAKI_CONTROLLER_BUTTON_PYRAMID, Qt::Key_Yes },
    { CHIAKI_CONTROLLER_BUTTON_L1, Qt::Key_PageUp },
    { CHIAKI_CONTROLLER_BUTTON_R1, Qt::Key_PageDown },
    { CHIAKI_CONTROLLER_BUTTON_OPTIONS, Qt::Key_Menu },
};

QmlController::QmlController(Controller *c, QObject *t, QObject *parent)
    : QObject(parent)
    , target(t)
    , controller(c)
{
    repeat_timer = new QTimer(this);
    connect(repeat_timer, &QTimer::timeout, this, [this]() {
        if (!repeat_running++)
            repeat_timer->start(80);
        sendKey(pressed_key);
        if (repeat_running == 50)
            repeat_timer->stop();
    });

    connect(controller, &Controller::StateChanged, this, [this]() {
        auto state = controller->GetState();
        auto buttons = state.buttons;

        if (state.left_x > 30000)
            buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
        else if (state.left_x < -30000)
            buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;

        if (state.left_y > 30000)
            buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
        else if (state.left_y < -30000)
            buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_UP;

        for (auto &k : std::as_const(key_map)) {
            const bool pressed = buttons & k.first;
            const bool old_pressed = old_buttons & k.first;
            if (pressed && !old_pressed) {
                pressed_key = k.second;
                sendKey(pressed_key);
                repeat_running = 0;
                repeat_timer->start(250);
            } else if (old_pressed && !pressed && pressed_key == k.second) {
                repeat_timer->stop();
                repeat_running = 1;
            }
        }
        old_buttons = buttons;
    });
}

bool QmlController::isDualSense() const
{
    return controller->IsDualSense();
}

bool QmlController::isSteamDeck() const
{
    return controller->IsSteamDeck();
}

void QmlController::sendKey(Qt::Key key)
{
    QKeyEvent *press = new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier);
    QKeyEvent *release = new QKeyEvent(QEvent::KeyRelease, key, Qt::NoModifier);
    QGuiApplication::postEvent(target, press);
    QGuiApplication::postEvent(target, release);
}
