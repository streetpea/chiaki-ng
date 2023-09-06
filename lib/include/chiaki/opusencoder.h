// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_OPUSENCODER_H
#define CHIAKI_OPUSENCODER_H

#include <chiaki/config.h>
#if CHIAKI_LIB_ENABLE_OPUS

#include "audiosender.h"
#include "session.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct chiaki_opus_encoder_t
{
	ChiakiLog *log;
	struct OpusEncoder *opus_encoder;
	ChiakiAudioHeader audio_header;
	uint8_t *opus_frame_buf;
	size_t opus_frame_buf_size;
	ChiakiAudioSender *audio_sender;
} ChiakiOpusEncoder;

CHIAKI_EXPORT void chiaki_opus_encoder_init(ChiakiOpusEncoder *encoder, ChiakiLog *log);
CHIAKI_EXPORT void chiaki_opus_encoder_fini(ChiakiOpusEncoder *encoder);
CHIAKI_EXPORT void chiaki_opus_encoder_header(ChiakiAudioHeader *header, ChiakiOpusEncoder *encoder, ChiakiSession *session);
CHIAKI_EXPORT void chiaki_opus_encoder_frame(int16_t *pcm_buf, ChiakiOpusEncoder *header);

#ifdef __cplusplus
}
#endif

#endif

#endif // CHIAKI_OPUSENCODER_H
