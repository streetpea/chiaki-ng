#include "windowsWakeSleep.h"
#include "qmlmainwindow.h"
extern "C" {
#include <powrprof.h>
}
#pragma comment(lib, "powrprof.lib")

static ULONG MySleepCallBack(PVOID windowsWakeSleep, ULONG messageType, PVOID Setting)
{
    
    WindowsWakeSleep *windows_wake_sleep = reinterpret_cast<WindowsWakeSleep *>(windowsWakeSleep);
    switch (messageType)
    {
        case PBT_APMSUSPEND:
            /* System is suspending
                Perform needed actions before system goes to sleep
            */
            windows_wake_sleep->aboutToSleep();

        case PBT_APMRESUMESUSPEND:

            /* System is resuming from suspend
                Perform needed action after waking up
            */
            windows_wake_sleep->awake();
            break;

        case PBT_APMRESUMEAUTOMATIC:

            /* System is resuming automatically (i.e., scheduled wake-up)
                Perform needed action after waking up
            */
            windows_wake_sleep->awake();
            break; 

        default:

            break;
    }
    return 0;
}

WindowsWakeSleep::WindowsWakeSleep(QObject *parent)
    : QObject(parent)
    , wake_state(WindowsWakeState::Awake)
{
#ifndef DEVICE_NOTIFY_CALLBACK
#define DEVICE_NOTIFY_CALLBACK 2
#endif
#ifndef DEVICE_NOTIFY_CALLBACK_ROUTINE
    typedef
    ULONG
    CALLBACK
    DEVICE_NOTIFY_CALLBACK_ROUTINE (
        _In_opt_ PVOID Context,
        _In_ ULONG Type,
        _In_ PVOID Setting
        );
    typedef DEVICE_NOTIFY_CALLBACK_ROUTINE* PDEVICE_NOTIFY_CALLBACK_ROUTINE;
#endif
#ifndef _DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS
    typedef struct _DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS {
        PDEVICE_NOTIFY_CALLBACK_ROUTINE Callback;
        PVOID Context;
    } DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS, *PDEVICE_NOTIFY_SUBSCRIBE_PARAMETERS;
#endif
    static DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS sleepCallback = {
        MySleepCallBack,
        this
    };
    // register to receive system sleep notifications
    hPowerNotify = RegisterSuspendResumeNotification(&sleepCallback, DEVICE_NOTIFY_CALLBACK);
    if (hPowerNotify == NULL)
    {
        qCCritical(chiakiGui) << "Failed to register for system sleep notifications with error:" << GetLastError();
    }
}

WindowsWakeSleep::~WindowsWakeSleep()
{
    // unregister for system sleep notifications
    UnregisterSuspendResumeNotification(hPowerNotify);
}

void WindowsWakeSleep::setWakeState(WindowsWakeState state)
{
    this->wake_state = state;
}

WindowsWakeState WindowsWakeSleep::getWakeState()
{
    return this->wake_state;
}

void WindowsWakeSleep::awake()
{
    emit wokeUp();
}

void WindowsWakeSleep::aboutToSleep()
{
    setWakeState(WindowsWakeState::AboutToSleep);
    emit sleeping();
}