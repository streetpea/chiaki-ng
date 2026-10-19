#ifndef MAC_MOUSE_LOOK_H
#define MAC_MOUSE_LOOK_H

#ifdef __cplusplus
extern "C" {
#endif

// macOS only: accumulates raw mouse movement even while the cursor is frozen.
// The source is GCMouse (unaccelerated, sub-pixel); NSEvent deltas are only a fallback.
void macMouseDeltaStart(void);
void macMouseDeltaStop(void);
void macMouseDeltaTake(double *dx, double *dy);

#ifdef __cplusplus
}
#endif

#endif
