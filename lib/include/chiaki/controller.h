// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_CONTROLLER_H
#define CHIAKI_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum chiaki_controller_button_t
{
	CHIAKI_CONTROLLER_BUTTON_CROSS 		= (1 << 0),
	CHIAKI_CONTROLLER_BUTTON_MOON 		= (1 << 1),
	CHIAKI_CONTROLLER_BUTTON_BOX 		= (1 << 2),
	CHIAKI_CONTROLLER_BUTTON_PYRAMID 	= (1 << 3),
	CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT 	= (1 << 4),
	CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT = (1 << 5),
	CHIAKI_CONTROLLER_BUTTON_DPAD_UP 	= (1 << 6),
	CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN 	= (1 << 7),
	CHIAKI_CONTROLLER_BUTTON_L1 		= (1 << 8),
	CHIAKI_CONTROLLER_BUTTON_R1 		= (1 << 9),
	CHIAKI_CONTROLLER_BUTTON_L3			= (1 << 10),
	CHIAKI_CONTROLLER_BUTTON_R3			= (1 << 11),
	CHIAKI_CONTROLLER_BUTTON_OPTIONS 	= (1 << 12),
	CHIAKI_CONTROLLER_BUTTON_SHARE 		= (1 << 13),
	CHIAKI_CONTROLLER_BUTTON_TOUCHPAD	= (1 << 14),
	CHIAKI_CONTROLLER_BUTTON_PS			= (1 << 15)
} ChiakiControllerButton;

#define CHIAKI_CONTROLLER_BUTTONS_COUNT 16

typedef enum chiaki_controller_analog_button_t
{
	// must not overlap with ChiakiControllerButton
	CHIAKI_CONTROLLER_ANALOG_BUTTON_L2 = (1 << 16),
	CHIAKI_CONTROLLER_ANALOG_BUTTON_R2 = (1 << 17)
} ChiakiControllerAnalogButton;

typedef struct chiaki_controller_touch_t
{
	uint16_t x, y;
	int8_t id; // -1 = up
} ChiakiControllerTouch;

#define CHIAKI_CONTROLLER_TOUCHES_MAX 2

typedef struct chiaki_controller_state_t
{
	/**
	 * Bitmask of ChiakiControllerButton
	 */
	uint32_t buttons;

	uint8_t l2_state;
	uint8_t r2_state;

	int16_t left_x;
	int16_t left_y;
	int16_t right_x;
	int16_t right_y;

	uint8_t touch_id_next;
	ChiakiControllerTouch touches[CHIAKI_CONTROLLER_TOUCHES_MAX];

	float gyro_x, gyro_y, gyro_z;
	float accel_x, accel_y, accel_z;
	float orient_x, orient_y, orient_z, orient_w;
} ChiakiControllerState;

CHIAKI_EXPORT void chiaki_controller_state_set_idle(ChiakiControllerState *state);

/**
 * @return A non-negative newly allocated touch id allocated or -1 if there are no slots left
 */
CHIAKI_EXPORT int8_t chiaki_controller_state_start_touch(ChiakiControllerState *state, uint16_t x, uint16_t y);

CHIAKI_EXPORT void chiaki_controller_state_stop_touch(ChiakiControllerState *state, uint8_t id);

CHIAKI_EXPORT void chiaki_controller_state_set_touch_pos(ChiakiControllerState *state, uint8_t id, uint16_t x, uint16_t y);

CHIAKI_EXPORT bool chiaki_controller_state_equals(ChiakiControllerState *a, ChiakiControllerState *b);

/**
 * Union of two controller states.
 * Ignores gyro, accel and orient because it makes no sense there.
 */
CHIAKI_EXPORT void chiaki_controller_state_or(ChiakiControllerState *out, ChiakiControllerState *a, ChiakiControllerState *b);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_CONTROLLER_H
