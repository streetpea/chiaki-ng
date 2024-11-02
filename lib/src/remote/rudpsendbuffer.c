// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_UNIT_TEST

#include <chiaki/remote/rudpsendbuffer.h>
#include <chiaki/time.h>

#include <string.h>
#include <assert.h>
#include <stdio.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#define RUDP_DATA_RESEND_TIMEOUT_MS 400
#define RUDP_DATA_RESEND_WAKEUP_TIMEOUT_MS (RUDP_DATA_RESEND_TIMEOUT_MS/2)
#define RUDP_DATA_RESEND_TRIES_MAX 10
#define RUDP_SEND_BUFFER_SIZE 16

#endif

struct chiaki_rudp_send_buffer_packet_t
{
	ChiakiSeqNum16 seq_num;
	uint64_t tries;
	uint64_t last_send_ms; // chiaki_time_now_monotonic_ms()
	uint8_t *buf;
	size_t buf_size;
}; // ChiakiRudpSendBufferPacket

#ifndef CHIAKI_UNIT_TEST

static void *rudp_send_buffer_thread_func(void *user);

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_buffer_init(ChiakiRudpSendBuffer *send_buffer, ChiakiRudp rudp, ChiakiLog *log, size_t size)
{
	ChiakiErrorCode err = chiaki_mutex_init(&send_buffer->mutex, false);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	chiaki_mutex_lock(&send_buffer->mutex);
	send_buffer->rudp = rudp;
	send_buffer->log = log;

	send_buffer->packets = calloc(size, sizeof(ChiakiRudpSendBufferPacket));
	if(!send_buffer->packets)
	{
		chiaki_mutex_unlock(&send_buffer->mutex);
		err = CHIAKI_ERR_MEMORY;
		goto error_mutex;
	}
	send_buffer->packets_size = size;
	send_buffer->packets_count = 0;

	send_buffer->should_stop = false;
	chiaki_mutex_unlock(&send_buffer->mutex);
	err = chiaki_cond_init(&send_buffer->cond);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_packets;

	err = chiaki_thread_create(&send_buffer->thread, rudp_send_buffer_thread_func, send_buffer);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_cond;

	chiaki_thread_set_name(&send_buffer->thread, "Chiaki Rudp Send Buffer");

	return CHIAKI_ERR_SUCCESS;
error_cond:
	chiaki_cond_fini(&send_buffer->cond);
error_packets:
	free(send_buffer->packets);
error_mutex:
	chiaki_mutex_fini(&send_buffer->mutex);
	return err;
}

CHIAKI_EXPORT void chiaki_rudp_send_buffer_fini(ChiakiRudpSendBuffer *send_buffer)
{
	send_buffer->should_stop = true;
	ChiakiErrorCode err = chiaki_cond_signal(&send_buffer->cond);
	assert(err == CHIAKI_ERR_SUCCESS);
	err = chiaki_thread_join(&send_buffer->thread, NULL);
	assert(err == CHIAKI_ERR_SUCCESS);

	for(size_t i=0; i<send_buffer->packets_count; i++)
		free(send_buffer->packets[i].buf);

	chiaki_cond_fini(&send_buffer->cond);
	chiaki_mutex_fini(&send_buffer->mutex);
	free(send_buffer->packets);
}

static void GetRudpPacketType(ChiakiRudpSendBuffer *send_buffer, uint16_t packet_type, char *ptype)
{
    RudpPacketType type = htons(packet_type);
    switch(type)
    {
        case INIT_REQUEST:
			strcpy(ptype, "Init Request");
			break;
        case INIT_RESPONSE:
            strcpy(ptype, "Init Response");
			break;
        case COOKIE_REQUEST:
			strcpy(ptype, "Cookie Request");
            break;
        case COOKIE_RESPONSE:
            strcpy(ptype, "Cookie Response");
			break;
        case SESSION_MESSAGE:
			strcpy(ptype, "Session Message");
            break;
        case STREAM_CONNECTION_SWITCH_ACK:
			strcpy(ptype, "Takion Switch Ack");
            break;
        case ACK:
            strcpy(ptype, "Ack");
			break;
        case CTRL_MESSAGE:
            strcpy(ptype, "Ctrl Message");
			break;
        case UNKNOWN:
            strcpy(ptype, "Unknown");
			break;
        case FINISH:
			strcpy(ptype, "Finish");
            break;
        default:
			sprintf(ptype, "Undefined packet type 0x%04x", type);
			break;
    }
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_buffer_push(ChiakiRudpSendBuffer *send_buffer, ChiakiSeqNum16 seq_num, uint8_t *buf, size_t buf_size)
{
	ChiakiErrorCode err = chiaki_mutex_lock(&send_buffer->mutex);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	if(send_buffer->packets_count >= send_buffer->packets_size)
	{
		CHIAKI_LOGE(send_buffer->log, "Rudp Send Buffer overflow");
		err = CHIAKI_ERR_OVERFLOW;
		goto beach;
	}

	for(size_t i=0; i<send_buffer->packets_count; i++)
	{
		if(send_buffer->packets[i].seq_num == seq_num)
		{
			CHIAKI_LOGE(send_buffer->log, "Tried to push duplicate seqnum into Rudp Send Buffer");
			err = CHIAKI_ERR_INVALID_DATA;
			goto beach;
		}
	}

	ChiakiRudpSendBufferPacket *packet = &send_buffer->packets[send_buffer->packets_count++];
	packet->seq_num = seq_num;
	packet->tries = 0;
	packet->last_send_ms = chiaki_time_now_monotonic_ms();
	packet->buf = buf;
	packet->buf_size = buf_size;

	CHIAKI_LOGV(send_buffer->log, "Pushed seq num %#lx into Rudp Send Buffer", (unsigned long)seq_num);

	if(send_buffer->packets_count == 1)
	{
		// buffer was empty before, so it will sleep without timeout => WAKE UP!!
		chiaki_cond_signal(&send_buffer->cond);
	}

beach:
	if(err != CHIAKI_ERR_SUCCESS)
		free(buf);
	chiaki_mutex_unlock(&send_buffer->mutex);
	return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_buffer_ack(ChiakiRudpSendBuffer *send_buffer, ChiakiSeqNum16 seq_num, ChiakiSeqNum16 *acked_seq_nums, size_t *acked_seq_nums_count)
{
	ChiakiErrorCode err = chiaki_mutex_lock(&send_buffer->mutex);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	if(acked_seq_nums_count)
		*acked_seq_nums_count = 0;

	size_t i;
	size_t shift = 0; // amount to shift back
	size_t shift_start = SIZE_MAX;
	for(i=0; i<send_buffer->packets_count; i++)
	{
		if(send_buffer->packets[i].seq_num == seq_num || chiaki_seq_num_16_lt(send_buffer->packets[i].seq_num, seq_num))
		{
			if(acked_seq_nums && acked_seq_nums_count)
				acked_seq_nums[(*acked_seq_nums_count)++] = send_buffer->packets[i].seq_num;

			free(send_buffer->packets[i].buf);
			if(shift_start == SIZE_MAX)
			{
				// first shift
				shift_start = i;
				shift = 1;
			}
			else if(shift_start + shift == i)
			{
				// still in the same gap
				shift++;
			}
			else
			{
				// new gap, do shift
				memmove(send_buffer->packets + shift_start,
						send_buffer->packets + shift_start + shift,
						(i - (shift_start + shift)) * sizeof(ChiakiRudpSendBufferPacket));
				// start new shift
				shift_start = i - shift;
				shift++;
			}
		}
	}

	if(shift_start != SIZE_MAX)
	{
		// do final shift
		if(shift_start + shift < send_buffer->packets_count)
		{
			memmove(send_buffer->packets + shift_start,
					send_buffer->packets + shift_start + shift,
					(send_buffer->packets_count - (shift_start + shift)) * sizeof(ChiakiRudpSendBufferPacket));
		}
		send_buffer->packets_count -= shift;
	}

	CHIAKI_LOGV(send_buffer->log, "Acked seq num %#lx from Rudp Send Buffer", (unsigned long)seq_num);

	chiaki_mutex_unlock(&send_buffer->mutex);
	return err;
}

static void rudp_send_buffer_resend(ChiakiRudpSendBuffer *send_buffer);

static bool rudp_send_buffer_check_pred_packets(void *user)
{
	ChiakiRudpSendBuffer *send_buffer = user;
	return send_buffer->should_stop;
}

static bool rudp_send_buffer_check_pred_no_packets(void *user)
{
	ChiakiRudpSendBuffer *send_buffer = user;
	return send_buffer->should_stop || send_buffer->packets_count;
}

static void *rudp_send_buffer_thread_func(void *user)
{
	ChiakiRudpSendBuffer *send_buffer = user;

	ChiakiErrorCode err = chiaki_mutex_lock(&send_buffer->mutex);
	if(err != CHIAKI_ERR_SUCCESS)
		return NULL;

	while(true)
	{
		if(send_buffer->packets_count) // if there are packets, wait with timeout
			err = chiaki_cond_timedwait_pred(&send_buffer->cond, &send_buffer->mutex, RUDP_DATA_RESEND_WAKEUP_TIMEOUT_MS, rudp_send_buffer_check_pred_packets, send_buffer);
		else // if not, wait without timeout, but also wakeup if packets become available
			err = chiaki_cond_wait_pred(&send_buffer->cond, &send_buffer->mutex, rudp_send_buffer_check_pred_no_packets, send_buffer);

		if(err != CHIAKI_ERR_SUCCESS && err != CHIAKI_ERR_TIMEOUT)
			break;

		if(send_buffer->should_stop)
			break;

		rudp_send_buffer_resend(send_buffer);
	}

	chiaki_mutex_unlock(&send_buffer->mutex);

	return NULL;
}

static void rudp_send_buffer_resend(ChiakiRudpSendBuffer *send_buffer)
{
	if(!send_buffer->rudp)
		return;

	uint64_t now = chiaki_time_now_monotonic_ms();

	for(size_t i=0; i<send_buffer->packets_count; i++)
	{
		ChiakiRudpSendBufferPacket *packet = &send_buffer->packets[i];
		if(now - packet->last_send_ms > RUDP_DATA_RESEND_TIMEOUT_MS)
		{
			if(packet->tries >= RUDP_DATA_RESEND_TRIES_MAX)
			{
				CHIAKI_LOGI(send_buffer->log, "Hit max retries of %d tries giving up on packet with seqnum %#lx", RUDP_DATA_RESEND_TRIES_MAX, (unsigned long)packet->seq_num);
				ChiakiSeqNum16 ack_seq_nums[RUDP_SEND_BUFFER_SIZE];
				size_t ack_seq_nums_count;
				chiaki_mutex_unlock(&send_buffer->mutex);
				chiaki_rudp_send_buffer_ack(send_buffer, packet->seq_num, ack_seq_nums, &ack_seq_nums_count);
				chiaki_mutex_lock(&send_buffer->mutex);
				if(i > 0)
					i-= 1;
				continue;
			}
			char packet_type[29] = {0};
			GetRudpPacketType(send_buffer, *((uint16_t *)(packet->buf + 6)), packet_type);
			CHIAKI_LOGI(send_buffer->log, "rudp Send Buffer re-sending packet with seqnum %#lx and type %s, tries: %llu", (unsigned long)packet->seq_num, packet_type, (unsigned long long)packet->tries);
			packet->last_send_ms = now;
			chiaki_rudp_send_raw(send_buffer->rudp, packet->buf, packet->buf_size);
			packet->tries++;
		}
	}
}

#endif