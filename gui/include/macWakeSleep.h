#ifndef MAC_WAKE_SLEEP_H
#define MAC_WAKE_SLEEP_H

#include "qmlbackend.h"
#include <QObject>
#include <IOKit/pwr_mgt/IOPMLib.h>

class MacWakeSleep : public QObject
{
	Q_OBJECT

public:
    MacWakeSleep(QmlBackend *backend);
    ~MacWakeSleep();
    void simulateUserActivity();
    void awake();
    QmlBackend *getBackend();
    io_connect_t getRootPort();

signals:
    void wokeUp();

private:
    IONotificationPortRef  notifyPortRef = {};
    io_object_t            notifierObject = {};
    io_connect_t           root_port = {};
    QmlBackend             *backend = {};
};

#endif