// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/config.h>
#if CHIAKI_LIB_ENABLE_OPUS

#include <chiaki/opusdecoder.h>

#include <opus/opus.h>

#include <string.h>

static void chiaki_opus_decoder_header(ChiakiAudioHeader *header, void *user);
static void chiaki_opus_decoder_frame(uint8_t *buf, size_t buf_size, void *user);

CHIAKI_EXPORT void chiaki_opus_decoder_init(ChiakiOpusDecoder *decoder, ChiakiLog *log)
{
	decoder->log = log;
	decoder->opus_decoder = NULL;
	memset(&decoder->audio_header, 0, sizeof(decoder->audio_header));

	decoder->pcm_buf = NULL;
	decoder->pcm_buf_size = 0;

	decoder->cb_user = NULL;
	decoder->settings_cb = NULL;
	decoder->frame_cb = NULL;
}

CHIAKI_EXPORT void chiaki_opus_decoder_fini(ChiakiOpusDecoder *decoder)
{
	free(decoder->pcm_buf);
	if(decoder->opus_decoder)
		opus_decoder_destroy(decoder->opus_decoder);
}

CHIAKI_EXPORT void chiaki_opus_decoder_get_sink(ChiakiOpusDecoder *decoder, ChiakiAudioSink *sink)
{
	sink->user = decoder;
	sink->header_cb = chiaki_opus_decoder_header;
	sink->frame_cb = chiaki_opus_decoder_frame;
}

static void chiaki_opus_decoder_header(ChiakiAudioHeader *header, void *user)
{
	ChiakiOpusDecoder *decoder = user;
	memcpy(&decoder->audio_header, header, sizeof(decoder->audio_header));

	opus_decoder_destroy(decoder->opus_decoder);

	int error;
	decoder->opus_decoder = opus_decoder_create(header->rate, header->channels, &error);

	if(error != OPUS_OK)
	{
		CHIAKI_LOGE(decoder->log, "ChiakiOpusDecoder failed to initialize opus decoder: %s", opus_strerror(error));
		decoder->opus_decoder = NULL;
		return;
	}

	CHIAKI_LOGI(decoder->log, "ChiakiOpusDecoder initialized");

	size_t pcm_buf_size_required = chiaki_audio_header_frame_buf_size(header);
	int16_t *pcm_buf_old = decoder->pcm_buf;
	if(!decoder->pcm_buf || decoder->pcm_buf_size != pcm_buf_size_required)
		decoder->pcm_buf = realloc(decoder->pcm_buf, pcm_buf_size_required);

	if(!decoder->pcm_buf)
	{
		free(pcm_buf_old);
		CHIAKI_LOGE(decoder->log, "ChiakiOpusDecoder failed to alloc pcm buffer");
		opus_decoder_destroy(decoder->opus_decoder);
		decoder->opus_decoder = NULL;
		decoder->pcm_buf_size = 0;
		return;
	}

	decoder->pcm_buf_size = pcm_buf_size_required;

	if(decoder->settings_cb)
		decoder->settings_cb(header->channels, header->rate, decoder->cb_user);
}

static void chiaki_opus_decoder_frame(uint8_t *buf, size_t buf_size, void *user)
{
	ChiakiOpusDecoder *decoder = user;
	if(!decoder->opus_decoder)
	{
		CHIAKI_LOGE(decoder->log, "Received audio frame, but opus decoder is not initialized");
		return;
	}

	int r = opus_decode(decoder->opus_decoder, buf, (opus_int32)buf_size, decoder->pcm_buf, decoder->audio_header.frame_size, 0);
	if(r < 1)
		CHIAKI_LOGE(decoder->log, "Decoding audio frame with opus failed: %s", opus_strerror(r));
	else if(decoder->frame_cb)
		decoder->frame_cb(decoder->pcm_buf, (size_t)r, decoder->cb_user);
}

#endif