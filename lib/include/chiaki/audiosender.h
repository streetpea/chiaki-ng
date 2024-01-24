// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_AUDIOSENDER_H
#define CHIAKI_AUDIOSENDER_H

#include "common.h"
#include "log.h"
#include "audio.h"
#include "takion.h"
#include "thread.h"
#include "session.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct chiaki_audio_sender_t
{
	ChiakiLog *log;
	ChiakiMutex mutex;
	bool ps5;
	ChiakiTakion *takion;
	uint16_t buf_size_per_unit;
	uint16_t buf_stride_per_unit;
	uint8_t *frame_buf;
	uint8_t *framea;
	uint8_t *frameb;
	size_t frame_buf_size;
	uint8_t *filled_packet_buf;
	ChiakiSeqNum16 frame_index;
} ChiakiAudioSender;

CHIAKI_EXPORT ChiakiErrorCode chiaki_audio_sender_init(ChiakiAudioSender *audio_sender, ChiakiLog *log, ChiakiSession *session);
CHIAKI_EXPORT void chiaki_audio_sender_fini(ChiakiAudioSender *audio_sender);
CHIAKI_EXPORT void chiaki_audio_sender_opus_data(ChiakiAudioSender *audio_sender, uint8_t *opus_data, size_t opus_data_size);

static inline ChiakiAudioSender *chiaki_audio_sender_new(ChiakiLog *log, ChiakiSession *session)
{
	ChiakiAudioSender *audio_sender = CHIAKI_NEW(ChiakiAudioSender);
	if(!audio_sender)
		return NULL;
	ChiakiErrorCode err = chiaki_audio_sender_init(audio_sender, log, session);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		free(audio_sender);
		return NULL;
	}
	return audio_sender;
}

static inline void chiaki_audio_sender_free(ChiakiAudioSender *audio_sender)
{
	if(!audio_sender)
		return;
	chiaki_audio_sender_fini(audio_sender);
	free(audio_sender);
}

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_AUDIOSENDER_H
