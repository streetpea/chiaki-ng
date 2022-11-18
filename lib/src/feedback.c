// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#define _USE_MATH_DEFINES

#include <chiaki/feedback.h>
#include <chiaki/controller.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif
#include <string.h>
#include <math.h>

#define GYRO_MIN -30.0f
#define GYRO_MAX 30.0f
#define ACCEL_MIN -5.0f
#define ACCEL_MAX 5.0f

static uint32_t compress_quat(float *q)
{
	// very similar idea as https://github.com/jpreiss/quatcompress
	size_t largest_i = 0;
	for(size_t i = 1; i < 4; i++)
	{
		if(fabs(q[i]) > fabs(q[largest_i]))
			largest_i = i;
	}
	uint32_t r = (q[largest_i] < 0.0 ? 1 : 0) | (largest_i << 1);
	for(size_t i = 0; i < 3; i++)
	{
		size_t qi = i < largest_i ? i : i + 1;
		float v = q[qi];
		if(v < -M_SQRT1_2)
			v = -M_SQRT1_2;
		if(v > M_SQRT1_2)
			v = M_SQRT1_2;
		v += M_SQRT1_2;
		v *= (float)0x1ff / (2.0f * M_SQRT1_2);
		r |= (uint32_t)v << (3 + i * 9);
	}
	return r;
}

CHIAKI_EXPORT void chiaki_feedback_state_format_v9(uint8_t *buf, ChiakiFeedbackState *state)
{
	buf[0x0] = 0xa0;
	uint16_t v = (uint16_t)(0xffff * ((float)state->gyro_x - GYRO_MIN) / (GYRO_MAX - GYRO_MIN));
	buf[0x1] = v;
	buf[0x2] = v >> 8;
	v = (uint16_t)(0xffff * ((float)state->gyro_y - GYRO_MIN) / (GYRO_MAX - GYRO_MIN));
	buf[0x3] = v;
	buf[0x4] = v >> 8;
	v = (uint16_t)(0xffff * ((float)state->gyro_z - GYRO_MIN) / (GYRO_MAX - GYRO_MIN));
	buf[0x5] = v;
	buf[0x6] = v >> 8;
	v = (uint16_t)(0xffff * ((float)state->accel_x - ACCEL_MIN) / (ACCEL_MAX - ACCEL_MIN));
	buf[0x7] = v;
	buf[0x8] = v >> 8;
	v = (uint16_t)(0xffff * ((float)state->accel_y - ACCEL_MIN) / (ACCEL_MAX - ACCEL_MIN));
	buf[0x9] = v;
	buf[0xa] = v >> 8;
	v = (uint16_t)(0xffff * ((float)state->accel_z - ACCEL_MIN) / (ACCEL_MAX - ACCEL_MIN));
	buf[0xb] = v;
	buf[0xc] = v >> 8;
	float q[4] = { state->orient_x, state->orient_y, state->orient_z, state->orient_w };
	uint32_t qc = compress_quat(q);
	buf[0xd] = qc;
	buf[0xe] = qc >> 0x8;
	buf[0xf] = qc >> 0x10;
	buf[0x10] = qc >> 0x18;
	*((chiaki_unaligned_uint16_t *)(buf + 0x11)) = htons((uint16_t)state->left_x);
	*((chiaki_unaligned_uint16_t *)(buf + 0x13)) = htons((uint16_t)state->left_y);
	*((chiaki_unaligned_uint16_t *)(buf + 0x15)) = htons((uint16_t)state->right_x);
	*((chiaki_unaligned_uint16_t *)(buf + 0x17)) = htons((uint16_t)state->right_y);
}

CHIAKI_EXPORT void chiaki_feedback_state_format_v12(uint8_t *buf, ChiakiFeedbackState *state, bool enable_dualsense)
{
	chiaki_feedback_state_format_v9(buf, state);
	buf[0x19] = 0x0;
	buf[0x1a] = 0x0;
	buf[0x1b] = enable_dualsense ? 0x0 : 0x1;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_history_event_set_button(ChiakiFeedbackHistoryEvent *event, uint64_t button, uint8_t state)
{
	// some buttons use a third byte for the state, some don't
	event->buf[0] = 0x80;
	event->len = 2;
	switch(button)
	{
		case CHIAKI_CONTROLLER_BUTTON_CROSS:
			event->buf[1] = 0x88;
			break;
		case CHIAKI_CONTROLLER_BUTTON_MOON:
			event->buf[1] = 0x89;
			break;
		case CHIAKI_CONTROLLER_BUTTON_BOX:
			event->buf[1] = 0x8a;
			break;
		case CHIAKI_CONTROLLER_BUTTON_PYRAMID:
			event->buf[1] = 0x8b;
			break;
		case CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT:
			event->buf[1] = 0x82;
			break;
		case CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT:
			event->buf[1] = 0x83;
			break;
		case CHIAKI_CONTROLLER_BUTTON_DPAD_UP:
			event->buf[1] = 0x80;
			break;
		case CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN:
			event->buf[1] = 0x81;
			break;
		case CHIAKI_CONTROLLER_BUTTON_L1:
			event->buf[1] = 0x84;
			break;
		case CHIAKI_CONTROLLER_BUTTON_R1:
			event->buf[1] = 0x85;
			break;
		case CHIAKI_CONTROLLER_ANALOG_BUTTON_L2:
			event->buf[1] = 0x86;
			break;
		case CHIAKI_CONTROLLER_ANALOG_BUTTON_R2:
			event->buf[1] = 0x87;
			break;
		case CHIAKI_CONTROLLER_BUTTON_L3:
			event->buf[1] = state ? 0xaf : 0x8f;
			return CHIAKI_ERR_SUCCESS;
		case CHIAKI_CONTROLLER_BUTTON_R3:
			event->buf[1] = state ? 0xb0 : 0x90;
			return CHIAKI_ERR_SUCCESS;
		case CHIAKI_CONTROLLER_BUTTON_OPTIONS:
			event->buf[1] = state ? 0xac : 0x8c;
			return CHIAKI_ERR_SUCCESS;
		case CHIAKI_CONTROLLER_BUTTON_SHARE:
			event->buf[1] = state ? 0xad : 0x8d;
			return CHIAKI_ERR_SUCCESS;
		case CHIAKI_CONTROLLER_BUTTON_TOUCHPAD:
			event->buf[1] = state ? 0xb1 : 0x91;
			return CHIAKI_ERR_SUCCESS;
		case CHIAKI_CONTROLLER_BUTTON_PS:
			event->buf[1] = state ? 0xae : 0x8e;
			return CHIAKI_ERR_SUCCESS;
		default:
			return CHIAKI_ERR_INVALID_DATA;
	}
	event->buf[2] = state;
	event->len = 3;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT void chiaki_feedback_history_event_set_touchpad(ChiakiFeedbackHistoryEvent *event,
		bool down, uint8_t pointer_id, uint16_t x, uint16_t y)
{
	event->len = 5;
	event->buf[0] = down ? 0xd0 : 0xc0;
	event->buf[1] = pointer_id & 0x7f;
	event->buf[2] = (uint8_t)(x >> 4);
	event->buf[3] = (uint8_t)((x & 0xf) << 4) | (uint8_t)(y >> 8);
	event->buf[4] = (uint8_t)y;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_history_buffer_init(ChiakiFeedbackHistoryBuffer *feedback_history_buffer, size_t size)
{
	feedback_history_buffer->events = calloc(size, sizeof(ChiakiFeedbackHistoryEvent));
	if(!feedback_history_buffer->events)
		return CHIAKI_ERR_MEMORY;
	feedback_history_buffer->size = size;
	feedback_history_buffer->begin = 0;
	feedback_history_buffer->len = 0;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT void chiaki_feedback_history_buffer_fini(ChiakiFeedbackHistoryBuffer *feedback_history_buffer)
{
	free(feedback_history_buffer->events);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_history_buffer_format(ChiakiFeedbackHistoryBuffer *feedback_history_buffer, uint8_t *buf, size_t *buf_size)
{
	size_t size_max = *buf_size;
	size_t written = 0;

	for(size_t i=0; i<feedback_history_buffer->len; i++)
	{
		ChiakiFeedbackHistoryEvent *event = &feedback_history_buffer->events[(feedback_history_buffer->begin + i) % feedback_history_buffer->size];
		if(written + event->len > size_max)
			return CHIAKI_ERR_BUF_TOO_SMALL;
		memcpy(buf + written, event->buf, event->len);
		written += event->len;
	}

	*buf_size = written;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT void chiaki_feedback_history_buffer_push(ChiakiFeedbackHistoryBuffer *feedback_history_buffer, ChiakiFeedbackHistoryEvent *event)
{
	feedback_history_buffer->begin = (feedback_history_buffer->begin + feedback_history_buffer->size - 1) % feedback_history_buffer->size;
	feedback_history_buffer->len++;
	if(feedback_history_buffer->len >= feedback_history_buffer->size)
		feedback_history_buffer->len = feedback_history_buffer->size;
	feedback_history_buffer->events[feedback_history_buffer->begin] = *event;
}
