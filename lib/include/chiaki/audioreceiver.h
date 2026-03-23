// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_AUDIORECEIVER_H
#define CHIAKI_AUDIORECEIVER_H

#include "common.h"
#include "log.h"
#include "audio.h"
#include "takion.h"
#include "thread.h"
#include "packetstats.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ChiakiAudioSinkHeader)(ChiakiAudioHeader *header, void *user);
typedef void (*ChiakiAudioSinkFrame)(uint8_t *buf, size_t buf_size, void *user);

/**
 * Sink that receives Audio encoded as Opus
 */
typedef struct chiaki_audio_sink_t
{
	void *user;
	ChiakiAudioSinkHeader header_cb;
	ChiakiAudioSinkFrame frame_cb;
} ChiakiAudioSink;

typedef struct chiaki_audio_receiver_t
{
	struct chiaki_session_t *session;
	ChiakiLog *log;
	ChiakiMutex mutex;
	ChiakiSeqNum16 frame_index_prev;
	ChiakiSeqNum16 next_frame_index;
	bool next_frame_index_valid;
	bool playback_started;
	bool frame_index_startup; // whether frame_index_prev has definitely not wrapped yet
	ChiakiPacketStats *packet_stats;
	struct {
		bool occupied;
		ChiakiSeqNum16 frame_index;
		uint8_t *buf;
		size_t buf_size;
	} jitter_buffer[8];
	size_t jitter_buffer_count;
} ChiakiAudioReceiver;

CHIAKI_EXPORT ChiakiErrorCode chiaki_audio_receiver_init(ChiakiAudioReceiver *audio_receiver, struct chiaki_session_t *session, ChiakiPacketStats *packet_stats);
CHIAKI_EXPORT void chiaki_audio_receiver_fini(ChiakiAudioReceiver *audio_receiver);
CHIAKI_EXPORT void chiaki_audio_receiver_stream_info(ChiakiAudioReceiver *audio_receiver, ChiakiAudioHeader *audio_header);
CHIAKI_EXPORT void chiaki_audio_receiver_av_packet(ChiakiAudioReceiver *audio_receiver, ChiakiTakionAVPacket *packet);

static inline ChiakiAudioReceiver *chiaki_audio_receiver_new(struct chiaki_session_t *session, ChiakiPacketStats *packet_stats)
{
	ChiakiAudioReceiver *audio_receiver = CHIAKI_NEW(ChiakiAudioReceiver);
	if(!audio_receiver)
		return NULL;
	ChiakiErrorCode err = chiaki_audio_receiver_init(audio_receiver, session, packet_stats);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		free(audio_receiver);
		return NULL;
	}
	return audio_receiver;
}

static inline void chiaki_audio_receiver_free(ChiakiAudioReceiver *audio_receiver)
{
	if(!audio_receiver)
		return;
	chiaki_audio_receiver_fini(audio_receiver);
	free(audio_receiver);
}

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_AUDIORECEIVER_H
