// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/audio.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

#ifdef __SWITCH__
#include <arpa/inet.h>
#endif

void chiaki_audio_header_load(ChiakiAudioHeader *audio_header, const uint8_t *buf)
{
	audio_header->channels = buf[0];
	audio_header->bits = buf[1];
	audio_header->rate = ntohl(*((chiaki_unaligned_uint32_t *)(buf + 2)));
	audio_header->frame_size = ntohl(*((chiaki_unaligned_uint32_t *)(buf + 6)));
	audio_header->unknown = ntohl(*((chiaki_unaligned_uint32_t *)(buf + 0xa)));
}

void chiaki_audio_header_save(ChiakiAudioHeader *audio_header, uint8_t *buf)
{
	buf[0] = audio_header->bits;
	buf[1] = audio_header->channels;
	*((chiaki_unaligned_uint32_t *)(buf + 2)) = htonl(audio_header->rate);
	*((chiaki_unaligned_uint32_t *)(buf + 6)) = htonl(audio_header->frame_size);
	*((chiaki_unaligned_uint32_t *)(buf + 0xa)) = htonl(audio_header->unknown);
}

void chiaki_audio_header_set(ChiakiAudioHeader * audio_header, uint8_t channels, uint8_t bits, uint32_t rate, uint32_t frame_size)
{
	audio_header->channels = channels;
	audio_header->bits = bits;
	audio_header->rate = rate;
	audio_header->frame_size = frame_size;
	audio_header->unknown = 1;
}
