// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_AUDIO_H
#define CHIAKI_AUDIO_H

#include <stdint.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CHIAKI_AUDIO_HEADER_SIZE 0xe

typedef struct chiaki_audio_header_t
{
	uint8_t channels;
	uint8_t bits;
	uint32_t rate;
	uint32_t frame_size;
	uint32_t unknown;
} ChiakiAudioHeader;

CHIAKI_EXPORT void chiaki_audio_header_set(ChiakiAudioHeader *audio_header, uint8_t channels, uint8_t bits, uint32_t rate, uint32_t frame_size);
CHIAKI_EXPORT void chiaki_audio_header_load(ChiakiAudioHeader *audio_header, const uint8_t *buf);
CHIAKI_EXPORT void chiaki_audio_header_save(ChiakiAudioHeader *audio_header, uint8_t *buf);

static inline size_t chiaki_audio_header_frame_buf_size(ChiakiAudioHeader *audio_header)
{
	return audio_header->frame_size * audio_header->channels * sizeof(int16_t);
}

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_AUDIO_H
