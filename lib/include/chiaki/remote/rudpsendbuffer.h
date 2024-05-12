// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_RUDPSENDBUFFER_H
#define CHIAKI_RUDPSENDBUFFER_H

#include "../common.h"
#include "../log.h"
#include "../thread.h"
#include "../seqnum.h"
#include "../sock.h"
#include "../remote/rudp.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct chiaki_rudp_send_buffer_packet_t ChiakiRudpSendBufferPacket;

typedef struct chiaki_rudp_send_buffer_t
{
	ChiakiLog *log;
	ChiakiRudp rudp;

	ChiakiRudpSendBufferPacket *packets;
	size_t packets_size; // allocated size
	size_t packets_count; // current count

	ChiakiMutex mutex;
	ChiakiCond cond;
	bool should_stop;
	ChiakiThread thread;
} ChiakiRudpSendBuffer;


/**
 * Init a Send Buffer and start a thread that automatically re-sends RUDP packets.
 *
 * @param sock if NULL, the Send Buffer thread will effectively do nothing (for unit testing)
 * @param size number of packet slots
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_buffer_init(ChiakiRudpSendBuffer *send_buffer, ChiakiRudp rudp, ChiakiLog *log, size_t size);
CHIAKI_EXPORT void chiaki_rudp_send_buffer_fini(ChiakiRudpSendBuffer *send_buffer);

/**
 * @param buf ownership of this is taken by the ChiakiRudpSendBuffer, which will free it automatically later!
 * On error, buf is freed immediately.
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_buffer_push(ChiakiRudpSendBuffer *send_buffer, ChiakiSeqNum16 seq_num, uint8_t *buf, size_t buf_size);

/**
 * @param acked_seq_nums optional array of size of at least send_buffer->packets_size where acked seq nums will be stored
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_buffer_ack(ChiakiRudpSendBuffer *send_buffer, ChiakiSeqNum16 seq_num, ChiakiSeqNum16 *acked_seq_nums, size_t *acked_seq_nums_count);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_RUDPSENDBUFFER_H
