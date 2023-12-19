#pragma once

#include "controllermanager.h"

class QTimer;

class QmlController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dualSense READ isDualSense CONSTANT)
    Q_PROPERTY(bool steamDeck READ isSteamDeck CONSTANT)

public:
    QmlController(Controller *controller, QObject *target, QObject *parent = nullptr);

    bool isDualSense() const;
    bool isSteamDeck() const;

private:
    void sendKey(Qt::Key key);

    QObject *target = {};
    uint32_t old_buttons = 0;
    Controller *controller = {};
    QTimer *repeat_timer = {};
    int repeat_running = 0;
    Qt::Key pressed_key = Qt::Key_unknown;
};
