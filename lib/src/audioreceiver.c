// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/audioreceiver.h>
#include <chiaki/session.h>

#include <string.h>

static void chiaki_audio_receiver_frame(ChiakiAudioReceiver *audio_receiver, ChiakiSeqNum16 frame_index, uint8_t *buf, size_t buf_size);

CHIAKI_EXPORT ChiakiErrorCode chiaki_audio_receiver_init(ChiakiAudioReceiver *audio_receiver, ChiakiSession *session, ChiakiPacketStats *packet_stats)
{
	audio_receiver->session = session;
	audio_receiver->log = session->log;
	audio_receiver->packet_stats = packet_stats;

	audio_receiver->frame_index_prev = 0;
	audio_receiver->frame_index_startup = true;

	ChiakiErrorCode err = chiaki_mutex_init(&audio_receiver->mutex, false);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	return CHIAKI_ERR_SUCCESS;
}


CHIAKI_EXPORT void chiaki_audio_receiver_fini(ChiakiAudioReceiver *audio_receiver)
{
#ifdef CHIAKI_LIB_ENABLE_OPUS
	opus_decoder_destroy(audio_receiver->opus_decoder);
#endif
	chiaki_mutex_fini(&audio_receiver->mutex);
}


CHIAKI_EXPORT void chiaki_audio_receiver_stream_info(ChiakiAudioReceiver *audio_receiver, ChiakiAudioHeader *audio_header)
{
	chiaki_mutex_lock(&audio_receiver->mutex);

	CHIAKI_LOGI(audio_receiver->log, "Audio Header:");
	CHIAKI_LOGI(audio_receiver->log, "  channels = %d", audio_header->channels);
	CHIAKI_LOGI(audio_receiver->log, "  bits = %d", audio_header->bits);
	CHIAKI_LOGI(audio_receiver->log, "  rate = %d", audio_header->rate);
	CHIAKI_LOGI(audio_receiver->log, "  frame size = %d", audio_header->frame_size);
	CHIAKI_LOGI(audio_receiver->log, "  unknown = %d", audio_header->unknown);

	if(audio_receiver->session->audio_sink.header_cb)
		audio_receiver->session->audio_sink.header_cb(audio_header, audio_receiver->session->audio_sink.user);

	chiaki_mutex_unlock(&audio_receiver->mutex);
}

CHIAKI_EXPORT void chiaki_audio_receiver_av_packet(ChiakiAudioReceiver *audio_receiver, ChiakiTakionAVPacket *packet)
{
	if(packet->codec != 5)
	{
		CHIAKI_LOGE(audio_receiver->log, "Received Audio Packet with unknown Codec");
		return;
	}

	// this is mostly observation-based, so may not necessarily cover everything yet.

	uint8_t source_units_count = chiaki_takion_av_packet_audio_source_units_count(packet);
	uint8_t fec_units_count = chiaki_takion_av_packet_audio_fec_units_count(packet);
	uint8_t unit_size = chiaki_takion_av_packet_audio_unit_size(packet);

	if(!packet->data_size)
	{
		CHIAKI_LOGE(audio_receiver->log, "Audio AV Packet is empty");
		return;
	}

	if((uint16_t)fec_units_count + (uint16_t)source_units_count != packet->units_in_frame_total)
	{
		CHIAKI_LOGE(audio_receiver->log, "Source Units + FEC Units != Total Units in Audio AV Packet");
		return;
	}

	if(packet->data_size != (size_t)unit_size * (size_t)packet->units_in_frame_total)
	{
		CHIAKI_LOGE(audio_receiver->log, "Audio AV Packet size mismatch %#llx vs %#llx",
				(unsigned long long)packet->data_size,
				(unsigned long long)(unit_size * packet->units_in_frame_total));
		return;
	}

	if(packet->frame_index > (1 << 15))
		audio_receiver->frame_index_startup = false;

	for(size_t i=0; i<source_units_count+fec_units_count; i++)
	{
		ChiakiSeqNum16 frame_index;
		if(i < source_units_count)
			frame_index = packet->frame_index + i;
		else
		{
			// fec
			size_t fec_index = i - source_units_count;

			// first packets will contain the same frame multiple times, ignore those
			if(audio_receiver->frame_index_startup && packet->frame_index + fec_index < fec_units_count + 1)
				continue;

			frame_index = packet->frame_index - fec_units_count + fec_index;
		}

		chiaki_audio_receiver_frame(audio_receiver, frame_index, packet->data + unit_size * i, unit_size);
	}

	if(audio_receiver->packet_stats)
		chiaki_packet_stats_push_seq(audio_receiver->packet_stats, packet->frame_index);
}

static void chiaki_audio_receiver_frame(ChiakiAudioReceiver *audio_receiver, ChiakiSeqNum16 frame_index, uint8_t *buf, size_t buf_size)
{
	chiaki_mutex_lock(&audio_receiver->mutex);

	if(!chiaki_seq_num_16_gt(frame_index, audio_receiver->frame_index_prev))
		goto beach;
	audio_receiver->frame_index_prev = frame_index;

	if(audio_receiver->session->audio_sink.frame_cb)
		audio_receiver->session->audio_sink.frame_cb(buf, buf_size, audio_receiver->session->audio_sink.user);

beach:
	chiaki_mutex_unlock(&audio_receiver->mutex);
}
