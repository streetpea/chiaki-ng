// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef _SETSU_H
#define _SETSU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct setsu_t Setsu;
typedef struct setsu_device_t SetsuDevice;
typedef int SetsuTrackingId;

typedef enum {
	SETSU_DEVICE_TYPE_TOUCHPAD,
	SETSU_DEVICE_TYPE_MOTION
} SetsuDeviceType;

typedef enum {
	/* New device available to connect.
	 * Event will have path and type set to the new device. */
	SETSU_EVENT_DEVICE_ADDED,

	/* Previously available device removed.
	 * Event will have path and type set to the removed device.
	 * Any SetsuDevice connected to this path will automatically
	 * be disconnected and their pointers will be invalid immediately
	 * after the callback for this event returns. */
	SETSU_EVENT_DEVICE_REMOVED,

	/* Touch down.
	 * Event will have dev and tracking_id set. */
	SETSU_EVENT_TOUCH_DOWN,

	/* Touch down.
	 * Event will have dev and tracking_id set. */
	SETSU_EVENT_TOUCH_UP,

	/* Touch position update.
	 * Event will have dev, tracking_id, x and y set. */
	SETSU_EVENT_TOUCH_POSITION,

	/* Event will have dev and button set. */
	SETSU_EVENT_BUTTON_DOWN,

	/* Event will have dev and button set. */
	SETSU_EVENT_BUTTON_UP,

	/* Event will have motion set. */
	SETSU_EVENT_MOTION
} SetsuEventType;

#define SETSU_BUTTON_0 (1u << 0)

typedef uint64_t SetsuButton;

typedef struct setsu_event_t
{
	SetsuEventType type;
	union
	{
		struct
		{
			const char *path;
			SetsuDeviceType dev_type;
		};
		struct
		{
			SetsuDevice *dev;
			union
			{
				struct
				{
					SetsuTrackingId tracking_id;
					uint32_t x, y;
				} touch;
				SetsuButton button;
				struct
				{
					float accel_x, accel_y, accel_z; // unit is 1G
					float gyro_x, gyro_y, gyro_z; // unit is rad/sec
					uint32_t timestamp; // microseconds
				} motion;
			};
		};
	};
} SetsuEvent;

typedef void (*SetsuEventCb)(SetsuEvent *event, void *user);

Setsu *setsu_new();
void setsu_free(Setsu *setsu);
void setsu_poll(Setsu *setsu, SetsuEventCb cb, void *user);
SetsuDevice *setsu_connect(Setsu *setsu, const char *path, SetsuDeviceType type);
void setsu_disconnect(Setsu *setsu, SetsuDevice *dev);
const char *setsu_device_get_path(SetsuDevice *dev);
uint32_t setsu_device_touchpad_get_width(SetsuDevice *dev);
uint32_t setsu_device_touchpad_get_height(SetsuDevice *dev);


#ifdef __cplusplus
}
#endif

#endif
