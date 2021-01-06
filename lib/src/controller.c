// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/controller.h>

#define TOUCH_ID_MASK 0x7f

CHIAKI_EXPORT void chiaki_controller_state_set_idle(ChiakiControllerState *state)
{
	state->buttons = 0;
	state->l2_state = 0;
	state->r2_state = 0;
	state->left_x = 0;
	state->left_y = 0;
	state->right_x = 0;
	state->right_y = 0;
	state->touch_id_next = 0;
	for(size_t i = 0; i < CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
	{
		state->touches[i].id = -1;
		state->touches[i].x = 0;
		state->touches[i].y = 0;
	}
	state->gyro_x = state->gyro_y = state->gyro_z = 0.0f;
	state->accel_x = 0.0f;
	state->accel_y = 1.0f;
	state->accel_z = 0.0f;
	state->orient_x = 0.0f;
	state->orient_y = 0.0f;
	state->orient_z = 0.0f;
	state->orient_w = 1.0f;
}

CHIAKI_EXPORT int8_t chiaki_controller_state_start_touch(ChiakiControllerState *state, uint16_t x, uint16_t y)
{
	for(size_t i = 0; i < CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
	{
		if(state->touches[i].id < 0)
		{
			state->touches[i].id = state->touch_id_next;
			state->touch_id_next = (state->touch_id_next + 1) & TOUCH_ID_MASK;
			state->touches[i].x = x;
			state->touches[i].y = y;
			return state->touches[i].id;
		}
	}
	return -1;
}

CHIAKI_EXPORT void chiaki_controller_state_stop_touch(ChiakiControllerState *state, uint8_t id)
{
	for(size_t i = 0; i < CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
	{
		if(state->touches[i].id == id)
		{
			state->touches[i].id = -1;
			break;
		}
	}
}

CHIAKI_EXPORT void chiaki_controller_state_set_touch_pos(ChiakiControllerState *state, uint8_t id, uint16_t x, uint16_t y)
{
	id &= TOUCH_ID_MASK;
	for(size_t i = 0; i < CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
	{
		if(state->touches[i].id == id)
		{
			state->touches[i].x = x;
			state->touches[i].y = y;
			break;
		}
	}
}

CHIAKI_EXPORT bool chiaki_controller_state_equals(ChiakiControllerState *a, ChiakiControllerState *b)
{
	if(!(a->buttons == b->buttons
		&& a->l2_state == b->l2_state
		&& a->r2_state == b->r2_state
		&& a->left_x == b->left_x
		&& a->left_y == b->left_y
		&& a->right_x == b->right_x
		&& a->right_y == b->right_y))
		return false;

	for(size_t i=0; i<CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
	{
		if(a->touches[i].id != b->touches[i].id)
			return false;
		if(a->touches[i].id >= 0 && (a->touches[i].x != b->touches[i].x || a->touches[i].y != b->touches[i].y))
			return false;
	}

#define CHECKF(n) if(a->n < b->n - 0.0000001f || a->n > b->n + 0.0000001f) return false
	CHECKF(gyro_x);
	CHECKF(gyro_y);
	CHECKF(gyro_z);
	CHECKF(accel_x);
	CHECKF(accel_y);
	CHECKF(accel_z);
	CHECKF(orient_x);
	CHECKF(orient_y);
	CHECKF(orient_z);
	CHECKF(orient_w);
#undef CHECKF

	return true;
}

#define MAX(a, b)	  ((a) > (b) ? (a) : (b))
#define ABS(a)		  ((a) > 0 ? (a) : -(a))
#define MAX_ABS(a, b) (ABS(a) > ABS(b) ? (a) : (b))

CHIAKI_EXPORT void chiaki_controller_state_or(ChiakiControllerState *out, ChiakiControllerState *a, ChiakiControllerState *b)
{
	out->buttons = a->buttons | b->buttons;
	out->l2_state = MAX(a->l2_state, b->l2_state);
	out->r2_state = MAX(a->r2_state, b->r2_state);
	out->left_x = MAX_ABS(a->left_x, b->left_x);
	out->left_y = MAX_ABS(a->left_y, b->left_y);
	out->right_x = MAX_ABS(a->right_x, b->right_x);
	out->right_y = MAX_ABS(a->right_y, b->right_y);

	out->touch_id_next = 0;
	for(size_t i = 0; i < CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
	{
		ChiakiControllerTouch *touch = a->touches[i].id >= 0 ? &a->touches[i] : (b->touches[i].id >= 0 ? &b->touches[i] : NULL);
		if(!touch)
		{
			out->touches[i].id = -1;
			out->touches[i].x = out->touches[i].y = 0;
			continue;
		}
		if(touch != &out->touches[i])
			out->touches[i] = *touch;
	}
}
