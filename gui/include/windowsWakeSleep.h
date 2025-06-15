#pragma once

#include <QObject>
#include <windows.h>

typedef enum windows_wake_state_t {
    Awake = 0,
    AboutToSleep = 1,
    Sleeping = 2
} WindowsWakeState;

class WindowsWakeSleep : public QObject
{
	Q_OBJECT

public:
    WindowsWakeSleep(QObject *parent = nullptr);
    ~WindowsWakeSleep();
    void awake();
    void aboutToSleep();
    WindowsWakeState getWakeState();
    void setWakeState(WindowsWakeState state);

signals:
    void wokeUp();
    void sleeping();

private:
    HPOWERNOTIFY hPowerNotify = {};
    WindowsWakeState wake_state = WindowsWakeState::Awake;
};