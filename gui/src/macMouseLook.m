#import <AppKit/AppKit.h>
#import <GameController/GameController.h>
#import "macMouseLook.h"

// Raw (unaccelerated, unrounded) mouse movement comes from GCMouse (macOS 11+).
// Movement seen through a local NSEvent monitor is used as a fallback (e.g. trackpads),
// only while GCMouse is not delivering anything.

static id g_monitor = nil;
static id g_connect_observer = nil;
static double g_dx = 0.0;
static double g_dy = 0.0;
static double g_last_raw_time = 0.0; // last time GCMouse delivered movement
static NSObject *g_lock = nil;

static NSObject *mouseLookLock(void)
{
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        g_lock = [[NSObject alloc] init];
    });
    return g_lock;
}

static void addRawDelta(double dx, double dy)
{
    double now = [NSProcessInfo processInfo].systemUptime;
    @synchronized(mouseLookLock()) {
        g_dx += dx;
        g_dy += dy;
        g_last_raw_time = now;
    }
}

static void attachRawMouse(GCMouse *mouse) API_AVAILABLE(macos(11.0))
{
    mouse.mouseInput.mouseMovedHandler = ^(GCMouseInput *input, float deltaX, float deltaY) {
        // GCMouse reports "up" as positive, screen coordinates (and NSEvent) use "down"
        addRawDelta(deltaX, -deltaY);
    };
}

static void detachRawMouse(GCMouse *mouse) API_AVAILABLE(macos(11.0))
{
    mouse.mouseInput.mouseMovedHandler = nil;
}

void macMouseDeltaStart(void)
{
    @synchronized(mouseLookLock()) {
        g_dx = 0.0;
        g_dy = 0.0;
        g_last_raw_time = 0.0;
    }

    if (@available(macOS 11.0, *)) {
        for (GCMouse *mouse in GCMouse.mice)
            attachRawMouse(mouse);
        if (!g_connect_observer) {
            g_connect_observer = [[NSNotificationCenter defaultCenter]
                addObserverForName:GCMouseDidConnectNotification
                            object:nil
                             queue:nil
                        usingBlock:^(NSNotification *note) {
                            attachRawMouse(note.object);
                        }];
        }
    }

    if (g_monitor)
        return;
    NSEventMask mask = NSEventMaskMouseMoved
        | NSEventMaskLeftMouseDragged
        | NSEventMaskRightMouseDragged
        | NSEventMaskOtherMouseDragged;
    g_monitor = [NSEvent addLocalMonitorForEventsMatchingMask:mask handler:^NSEvent *(NSEvent *event) {
        double now = [NSProcessInfo processInfo].systemUptime;
        @synchronized(mouseLookLock()) {
            // fallback: ignored while GCMouse is delivering raw movement
            if (now - g_last_raw_time > 0.1) {
                g_dx += event.deltaX;
                g_dy += event.deltaY;
            }
        }
        return event;
    }];
}

void macMouseDeltaStop(void)
{
    if (@available(macOS 11.0, *)) {
        for (GCMouse *mouse in GCMouse.mice)
            detachRawMouse(mouse);
        if (g_connect_observer) {
            [[NSNotificationCenter defaultCenter] removeObserver:g_connect_observer];
            g_connect_observer = nil;
        }
    }
    if (g_monitor)
    {
        [NSEvent removeMonitor:g_monitor];
        g_monitor = nil;
    }
}

void macMouseDeltaTake(double *dx, double *dy)
{
    @synchronized(mouseLookLock()) {
        *dx = g_dx;
        *dy = g_dy;
        g_dx = 0.0;
        g_dy = 0.0;
    }
}
