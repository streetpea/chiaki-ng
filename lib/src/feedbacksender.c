// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/feedbacksender.h>

#define FEEDBACK_STATE_TIMEOUT_MIN_MS 8 // minimum time to wait between sending 2 packets
#define FEEDBACK_STATE_TIMEOUT_MAX_MS 200 // maximum time to wait between sending 2 packets

#define FEEDBACK_HISTORY_BUFFER_SIZE 0x10

static void *feedback_sender_thread_func(void *user);

CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_sender_init(ChiakiFeedbackSender *feedback_sender, ChiakiTakion *takion)
{
	feedback_sender->log = takion->log;
	feedback_sender->takion = takion;

	chiaki_controller_state_set_idle(&feedback_sender->controller_state_prev);
	chiaki_controller_state_set_idle(&feedback_sender->controller_state);

	feedback_sender->state_seq_num = 0;

	feedback_sender->history_seq_num = 0;
	ChiakiErrorCode err = chiaki_feedback_history_buffer_init(&feedback_sender->history_buf, FEEDBACK_HISTORY_BUFFER_SIZE);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	err = chiaki_mutex_init(&feedback_sender->state_mutex, false);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_history_buffer;

	err = chiaki_cond_init(&feedback_sender->state_cond);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_mutex;

	err = chiaki_thread_create(&feedback_sender->thread, feedback_sender_thread_func, feedback_sender);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_cond;

	chiaki_thread_set_name(&feedback_sender->thread, "Chiaki Feedback Sender");

	return CHIAKI_ERR_SUCCESS;
error_cond:
	chiaki_cond_fini(&feedback_sender->state_cond);
error_mutex:
	chiaki_mutex_fini(&feedback_sender->state_mutex);
error_history_buffer:
	chiaki_feedback_history_buffer_fini(&feedback_sender->history_buf);
	return err;
}

CHIAKI_EXPORT void chiaki_feedback_sender_fini(ChiakiFeedbackSender *feedback_sender)
{
	chiaki_mutex_lock(&feedback_sender->state_mutex);
	feedback_sender->should_stop = true;
	chiaki_mutex_unlock(&feedback_sender->state_mutex);
	chiaki_cond_signal(&feedback_sender->state_cond);
	chiaki_thread_join(&feedback_sender->thread, NULL);
	chiaki_cond_fini(&feedback_sender->state_cond);
	chiaki_mutex_fini(&feedback_sender->state_mutex);
	chiaki_feedback_history_buffer_fini(&feedback_sender->history_buf);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_feedback_sender_set_controller_state(ChiakiFeedbackSender *feedback_sender, ChiakiControllerState *state)
{
	ChiakiErrorCode err = chiaki_mutex_lock(&feedback_sender->state_mutex);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	if(chiaki_controller_state_equals(&feedback_sender->controller_state, state))
	{
		chiaki_mutex_unlock(&feedback_sender->state_mutex);
		return CHIAKI_ERR_SUCCESS;
	}

	feedback_sender->controller_state = *state;
	feedback_sender->controller_state_changed = true;

	chiaki_mutex_unlock(&feedback_sender->state_mutex);
	chiaki_cond_signal(&feedback_sender->state_cond);

	return CHIAKI_ERR_SUCCESS;
}

static bool controller_state_equals_for_feedback_state(ChiakiControllerState *a, ChiakiControllerState *b)
{
	if(!(a->left_x == b->left_x
		&& a->left_y == b->left_y
		&& a->right_x == b->right_x
		&& a->right_y == b->right_y))
		return false;
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

static void feedback_sender_send_state(ChiakiFeedbackSender *feedback_sender)
{
	ChiakiFeedbackState state;
	state.left_x = feedback_sender->controller_state.left_x;
	state.left_y = feedback_sender->controller_state.left_y;
	state.right_x = feedback_sender->controller_state.right_x;
	state.right_y = feedback_sender->controller_state.right_y;
	state.gyro_x = feedback_sender->controller_state.gyro_x;
	state.gyro_y = feedback_sender->controller_state.gyro_y;
	state.gyro_z = feedback_sender->controller_state.gyro_z;
	state.accel_x = feedback_sender->controller_state.accel_x;
	state.accel_y = feedback_sender->controller_state.accel_y;
	state.accel_z = feedback_sender->controller_state.accel_z;
	state.orient_x = feedback_sender->controller_state.orient_x;
	state.orient_y = feedback_sender->controller_state.orient_y;
	state.orient_z = feedback_sender->controller_state.orient_z;
	state.orient_w = feedback_sender->controller_state.orient_w;
	ChiakiErrorCode err = chiaki_takion_send_feedback_state(feedback_sender->takion, feedback_sender->state_seq_num++, &state);
	if(err != CHIAKI_ERR_SUCCESS)
		CHIAKI_LOGE(feedback_sender->log, "FeedbackSender failed to send Feedback State");
}

static bool controller_state_equals_for_feedback_history(ChiakiControllerState *a, ChiakiControllerState *b)
{
	if(!(a->buttons == b->buttons
		&& a->l2_state == b->l2_state
		&& a->r2_state == b->r2_state))
		return false;

	for(size_t i=0; i<CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
	{
		if(a->touches[i].id != b->touches[i].id)
			return false;
		if(a->touches[i].id >= 0 && (a->touches[i].x != b->touches[i].x || a->touches[i].y != b->touches[i].y))
			return false;
	}
	return true;
}

static void feedback_sender_send_history_packet(ChiakiFeedbackSender *feedback_sender)
{
	uint8_t buf[0x300];
	size_t buf_size = sizeof(buf);
	ChiakiErrorCode err = chiaki_feedback_history_buffer_format(&feedback_sender->history_buf, buf, &buf_size);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(feedback_sender->log, "Feedback Sender failed to format history buffer");
		return;
	}

	//CHIAKI_LOGD(feedback_sender->log, "Feedback History:");
	//chiaki_log_hexdump(feedback_sender->log, CHIAKI_LOG_DEBUG, buf, buf_size);
	chiaki_takion_send_feedback_history(feedback_sender->takion, feedback_sender->history_seq_num++, buf, buf_size);
}

static void feedback_sender_send_history(ChiakiFeedbackSender *feedback_sender)
{
	ChiakiControllerState *state_prev = &feedback_sender->controller_state_prev;
	ChiakiControllerState *state_now = &feedback_sender->controller_state;
	uint64_t buttons_prev = state_prev->buttons;
	uint64_t buttons_now = state_now->buttons;
	for(uint8_t i=0; i<CHIAKI_CONTROLLER_BUTTONS_COUNT; i++)
	{
		uint64_t button_id = 1 << i;
		bool prev = buttons_prev & button_id;
		bool now = buttons_now & button_id;
		if(prev != now)
		{
			ChiakiFeedbackHistoryEvent event;
			ChiakiErrorCode err = chiaki_feedback_history_event_set_button(&event, button_id, now ? 0xff : 0);
			if(err != CHIAKI_ERR_SUCCESS)
			{
				CHIAKI_LOGE(feedback_sender->log, "Feedback Sender failed to format button history event for button id %llu", (unsigned long long)button_id);
				continue;
			}
			chiaki_feedback_history_buffer_push(&feedback_sender->history_buf, &event);
			feedback_sender_send_history_packet(feedback_sender);
		}
	}

	if(state_prev->l2_state != state_now->l2_state)
	{
		ChiakiFeedbackHistoryEvent event;
		ChiakiErrorCode err = chiaki_feedback_history_event_set_button(&event, CHIAKI_CONTROLLER_ANALOG_BUTTON_L2, state_now->l2_state);
		if(err == CHIAKI_ERR_SUCCESS)
		{
			chiaki_feedback_history_buffer_push(&feedback_sender->history_buf, &event);
			feedback_sender_send_history_packet(feedback_sender);
		}
		else
			CHIAKI_LOGE(feedback_sender->log, "Feedback Sender failed to format button history event for L2");
	}

	if(state_prev->r2_state != state_now->r2_state)
	{
		ChiakiFeedbackHistoryEvent event;
		ChiakiErrorCode err = chiaki_feedback_history_event_set_button(&event, CHIAKI_CONTROLLER_ANALOG_BUTTON_R2, state_now->r2_state);
		if(err == CHIAKI_ERR_SUCCESS)
		{
			chiaki_feedback_history_buffer_push(&feedback_sender->history_buf, &event);
			feedback_sender_send_history_packet(feedback_sender);
		}
		else
			CHIAKI_LOGE(feedback_sender->log, "Feedback Sender failed to format button history event for R2");
	}

	for(size_t i=0; i<CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
	{
		if(state_prev->touches[i].id != state_now->touches[i].id && state_prev->touches[i].id >= 0)
		{
			ChiakiFeedbackHistoryEvent event;
			chiaki_feedback_history_event_set_touchpad(&event, false, (uint8_t)state_prev->touches[i].id,
					state_prev->touches[i].x, state_prev->touches[i].y);
			chiaki_feedback_history_buffer_push(&feedback_sender->history_buf, &event);
			feedback_sender_send_history_packet(feedback_sender);
		}
		else if(state_now->touches[i].id >= 0
				&& (state_prev->touches[i].id != state_now->touches[i].id
					|| state_prev->touches[i].x != state_now->touches[i].x
					|| state_prev->touches[i].y != state_now->touches[i].y))
		{
			ChiakiFeedbackHistoryEvent event;
			chiaki_feedback_history_event_set_touchpad(&event, true, (uint8_t)state_now->touches[i].id,
					state_now->touches[i].x, state_now->touches[i].y);
			chiaki_feedback_history_buffer_push(&feedback_sender->history_buf, &event);
			feedback_sender_send_history_packet(feedback_sender);
		}
	}
}

static bool state_cond_check(void *user)
{
	ChiakiFeedbackSender *feedback_sender = user;
	return feedback_sender->should_stop || feedback_sender->controller_state_changed;
}

static void *feedback_sender_thread_func(void *user)
{
	ChiakiFeedbackSender *feedback_sender = user;

	ChiakiErrorCode err = chiaki_mutex_lock(&feedback_sender->state_mutex);
	if(err != CHIAKI_ERR_SUCCESS)
		return NULL;

	uint64_t next_timeout = FEEDBACK_STATE_TIMEOUT_MAX_MS;
	while(true)
	{
		err = chiaki_cond_timedwait_pred(&feedback_sender->state_cond, &feedback_sender->state_mutex, next_timeout, state_cond_check, feedback_sender);
		if(err != CHIAKI_ERR_SUCCESS && err != CHIAKI_ERR_TIMEOUT)
			break;

		if(feedback_sender->should_stop)
			break;

		bool send_feedback_state = true;
		bool send_feedback_history = false;

		if(feedback_sender->controller_state_changed)
		{
			// TODO: FEEDBACK_STATE_TIMEOUT_MIN_MS
			feedback_sender->controller_state_changed = false;

			// don't need to send feedback state if nothing relevant changed
			if(controller_state_equals_for_feedback_state(&feedback_sender->controller_state, &feedback_sender->controller_state_prev))
				send_feedback_state = false;

			send_feedback_history = !controller_state_equals_for_feedback_history(&feedback_sender->controller_state, &feedback_sender->controller_state_prev);
		} // else: timeout

		if(send_feedback_state)
			feedback_sender_send_state(feedback_sender);

		if(send_feedback_history)
			feedback_sender_send_history(feedback_sender);

		feedback_sender->controller_state_prev = feedback_sender->controller_state;
	}

	chiaki_mutex_unlock(&feedback_sender->state_mutex);

	return NULL;
}
