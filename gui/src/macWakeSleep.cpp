#include "macWakeSleep.h"
#include <mach/mach_port.h>
#include <mach/mach_interface.h>
#include <mach/mach_init.h>
#include <IOKit/IOMessage.h>

void MySleepCallBack(void *macWakeSleep, io_service_t service, natural_t messageType, void *messageArgument)
{
    MacWakeSleep *mac_wake_sleep = reinterpret_cast<MacWakeSleep *>(macWakeSleep);
    io_connect_t root_port = mac_wake_sleep->getRootPort();
    switch (messageType)
    {
        case kIOMessageCanSystemSleep:
            /* Idle sleep is about to kick in. This message will not be sent for forced sleep.

                Applications have a chance to prevent sleep by calling IOCancelPowerChange.

                Most applications should not prevent idle sleep.

                Power Management waits up to 30 seconds for you to either allow or deny idle

                sleep. If you don't acknowledge this power change by calling either

                IOAllowPowerChange or IOCancelPowerChange, the system will wait 30

                seconds then go to sleep.

            */

 

            //Uncomment to cancel idle sleep

            //IOCancelPowerChange( root_port, (long)messageArgument );

            // we will allow idle sleep

            IOAllowPowerChange( root_port, (long)messageArgument );

            break;

 

        case kIOMessageSystemWillSleep:

            /* The system WILL go to sleep. If you do not call IOAllowPowerChange or

                IOCancelPowerChange to acknowledge this message, sleep will be

                delayed by 30 seconds.

 

                NOTE: If you call IOCancelPowerChange to deny sleep it returns

                kIOReturnSuccess, however the system WILL still go to sleep.

            */
            mac_wake_sleep->getBackend()->goToSleep();
            IOAllowPowerChange( root_port, (long)messageArgument );

            break;

 

        case kIOMessageSystemWillPowerOn:

            //System has started the wake up process...

            break;

 

        case kIOMessageSystemHasPoweredOn:

            //System has finished waking up...
            mac_wake_sleep->awake();

        break;

 

        default:

            break;

 

    }

}

MacWakeSleep::MacWakeSleep(QmlBackend *backend)
    : QObject(backend)
    , backend(backend)
{
    // register to receive system sleep notifications
    root_port = IORegisterForSystemPower(this, &notifyPortRef, MySleepCallBack, &notifierObject );
    if ( root_port == 0 )
    {
        qCCritical(chiakiGui) << "IORegisterForSystemPower failed";
    }
    
    // add the notification port to the application runloop
    CFRunLoopAddSource( CFRunLoopGetCurrent(),
            IONotificationPortGetRunLoopSource(notifyPortRef), kCFRunLoopCommonModes );
}

MacWakeSleep::~MacWakeSleep()
{
    // we no longer want sleep notifications:
    // remove the sleep notification port from the application runloop

    CFRunLoopRemoveSource( CFRunLoopGetCurrent(),

                           IONotificationPortGetRunLoopSource(notifyPortRef),

                           kCFRunLoopCommonModes );

    // deregister for system sleep notifications
    IODeregisterForSystemPower( &notifierObject );

    // IORegisterForSystemPower implicitly opens the Root Power Domain IOService

    // so we close it here
    IOServiceClose( root_port );

    // destroy the notification port allocated by IORegisterForSystemPower
    IONotificationPortDestroy( notifyPortRef );
}

void MacWakeSleep::simulateUserActivity()
{
    IOPMAssertionID id = 0;
    IOReturn rc = 0;
    rc = IOPMAssertionDeclareUserActivity(CFSTR("chiaki-ng controller input"), kIOPMUserActiveLocal, &id);
    if(rc == kIOReturnSuccess)
        qCDebug(chiakiGui) << "Successfully sent controller activity to MacOS with ID:" << id;
    else
        qCCritical(chiakiGui) << "Failed sending controller activity to MacOS";  
}

void MacWakeSleep::awake()
{
    emit wokeUp();
}

QmlBackend *MacWakeSleep::getBackend()
{
    return backend;
}

io_connect_t MacWakeSleep::getRootPort()
{
    return root_port;
}