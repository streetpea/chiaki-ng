#pragma once

#include <QObject>
#include <windows.h>

class WindowsWakeSleep : public QObject
{
	Q_OBJECT

public:
    WindowsWakeSleep(QObject *parent = nullptr);
    ~WindowsWakeSleep();
    void awake();
    void aboutToSleep();

signals:
    void wokeUp();
    void sleeping();

private:
    HPOWERNOTIFY hPowerNotify = {};
};