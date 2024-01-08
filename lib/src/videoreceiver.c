// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/videoreceiver.h>
#include <chiaki/session.h>
#include <chiaki/vl_rbsp.h>

#include <string.h>

static ChiakiErrorCode chiaki_video_receiver_flush_frame(ChiakiVideoReceiver *video_receiver);

static void add_ref_frame(ChiakiVideoReceiver *video_receiver, int32_t frame)
{
	int oldest_index = -1;
	int32_t oldest_frame = INT32_MAX;
	for(int i=0; i<16; i++)
	{
		int32_t ref_frame = video_receiver->reference_frames[i];
		if(ref_frame > 0 && (oldest_frame == INT32_MAX || chiaki_seq_num_16_lt((ChiakiSeqNum16)ref_frame, (ChiakiSeqNum16)oldest_frame)))
		{
			oldest_index = i;
			oldest_frame = ref_frame;
		}
		if(ref_frame == -1)
		{
			video_receiver->reference_frames[i] = frame;
			return;
		}
	}
	video_receiver->reference_frames[oldest_index] = frame;
}

static bool have_ref_frame(ChiakiVideoReceiver *video_receiver, int32_t frame)
{
	for(int i=0; i<16; i++)
		if(video_receiver->reference_frames[i] == frame)
			return true;
	return false;
}

#define SLICE_TYPE_UNKNOWN 0
#define SLICE_TYPE_I 1
#define SLICE_TYPE_P 2

static bool parse_slice_h264(ChiakiVideoReceiver *video_receiver, uint8_t *data, unsigned size, unsigned *slice_type, unsigned *ref_frame)
{
	struct vl_vlc vlc = {0};
	vl_vlc_init(&vlc, data, size);
	if(vl_vlc_peekbits(&vlc, 32) != 1)
	{
		CHIAKI_LOGW(video_receiver->log, "parse_slice_h264: No startcode found");
		return false;
	}
	vl_vlc_eatbits(&vlc, 32);
	vl_vlc_fillbits(&vlc);

	vl_vlc_eatbits(&vlc, 1); // forbidden_zero_bit
	vl_vlc_eatbits(&vlc, 2); // nal_ref_idc
	unsigned nal_unit_type = vl_vlc_get_uimsbf(&vlc, 5);

	if(nal_unit_type != 1 && nal_unit_type != 5)
	{
		CHIAKI_LOGW(video_receiver->log, "parse_slice_h264: Unexpected NAL unit type %u", nal_unit_type);
		return false;
	}

	struct vl_rbsp rbsp;
	vl_rbsp_init(&rbsp, &vlc, ~0);
	vl_rbsp_ue(&rbsp); // first_mb_in_slice

	switch(vl_rbsp_ue(&rbsp))
	{
		case 0:
		case 5:
			*slice_type = SLICE_TYPE_P;
			break;
		case 2:
		case 7:
			*slice_type = SLICE_TYPE_I;
			break;
		default:
			*slice_type = SLICE_TYPE_UNKNOWN;
			break;
	}

	if(nal_unit_type == 1)
	{
		*ref_frame = 0;
		vl_rbsp_ue(&rbsp); // pic_parameter_set_id
		vl_rbsp_u(&rbsp, 7); // frame_num FIXME: should be u(log2_max_frame_num_minus4 + 4)
		if(vl_rbsp_u(&rbsp, 1)) // num_ref_idx_active_override_flag
			if(vl_rbsp_u(&rbsp, 1)) // num_ref_idx_active_override_flag
				vl_rbsp_ue(&rbsp); // num_ref_idx_l0_active_minus1
		if(vl_rbsp_u(&rbsp, 1)) // ref_pic_list_modification_flag_l0
		{
			unsigned i = 0;
			unsigned modification_of_pic_nums_idc = vl_rbsp_ue(&rbsp);
			while(i++<3)
			{
				if(modification_of_pic_nums_idc == 0)
					*ref_frame = vl_rbsp_ue(&rbsp); // abs_diff_pic_num_minus1
				else if(modification_of_pic_nums_idc < 3)
					vl_rbsp_ue(&rbsp); // abs_diff_pic_num_minus1 or long_term_pic_num
				else if(modification_of_pic_nums_idc == 3)
					return true;
				else
					break;
				modification_of_pic_nums_idc = vl_rbsp_ue(&rbsp);
			}
			CHIAKI_LOGW(video_receiver->log, "parse_slice_h264: Failed to parse ref_pic_list_modification");
			return false;
		}
	}

	return true;
}

static bool parse_slice_h265(ChiakiVideoReceiver *video_receiver, uint8_t *data, unsigned size, unsigned *slice_type, unsigned *ref_frame)
{
	struct vl_vlc vlc = {0};
	vl_vlc_init(&vlc, data, size);
	if(vl_vlc_peekbits(&vlc, 32) != 1)
	{
		CHIAKI_LOGW(video_receiver->log, "parse_slice_h265: No startcode found");
		return false;
	}
	vl_vlc_eatbits(&vlc, 32);
	vl_vlc_fillbits(&vlc);

	vl_vlc_eatbits(&vlc, 1); // forbidden_zero_bit
	unsigned nal_unit_type = vl_vlc_get_uimsbf(&vlc, 6);
	vl_vlc_eatbits(&vlc, 6); // nuh_layer_id
	vl_vlc_eatbits(&vlc, 3); // nuh_temporal_id_plus1

	if(nal_unit_type != 1 && nal_unit_type != 20)
	{
		CHIAKI_LOGW(video_receiver->log, "parse_slice_h265: Unexpected NAL unit type %u", nal_unit_type);
		return false;
	}

	struct vl_rbsp rbsp;
	vl_rbsp_init(&rbsp, &vlc, ~0);
	unsigned first_slice_segment_in_pic_flag = vl_rbsp_u(&rbsp, 1);
	if(nal_unit_type == 20)
		vl_rbsp_u(&rbsp, 1); // no_output_of_prior_pics_flag

	vl_rbsp_ue(&rbsp); // slice_pic_parameter_set_id
	if(!first_slice_segment_in_pic_flag)
		vl_rbsp_ue(&rbsp); // slice_segment_address

	switch(vl_rbsp_ue(&rbsp))
	{
		case 1:
			*slice_type = SLICE_TYPE_P;
			break;
		case 2:
			*slice_type = SLICE_TYPE_I;
			break;
		default:
			*slice_type = SLICE_TYPE_UNKNOWN;
			break;
	}

	if(nal_unit_type == 1)
	{
		*ref_frame = 0xff;
		vl_rbsp_u(&rbsp, 4); // slice_pic_order_cnt_lsb FIXME: should be u(log2_max_pic_order_cnt_lsb_minus4 + 4)
		if(!vl_rbsp_u(&rbsp, 1)) // short_term_ref_pic_set_sps_flag
		{
			unsigned num_negative_pics = vl_rbsp_ue(&rbsp);
			if(num_negative_pics > 16)
			{
				CHIAKI_LOGW(video_receiver->log, "parse_slice_h265: Unexpected num_negative_pics %u", num_negative_pics);
				return false;
			}
			vl_rbsp_ue(&rbsp); // num_positive_pics
			for(unsigned i=0; i<num_negative_pics; i++)
			{
				vl_rbsp_ue(&rbsp); // delta_poc_s0_minus1[i]
				if(vl_rbsp_u(&rbsp, 1)) // used_by_curr_pic_s0_flag[i]
				{
					*ref_frame = i;
					break;
				}
			}
		}
		if(*ref_frame == 0xff)
			CHIAKI_LOGV(video_receiver->log, "parse_slice_h265: No ref frame found");
	}

	return true;
}

CHIAKI_EXPORT void chiaki_video_receiver_init(ChiakiVideoReceiver *video_receiver, struct chiaki_session_t *session, ChiakiPacketStats *packet_stats)
{
	video_receiver->session = session;
	video_receiver->log = session->log;
	memset(video_receiver->profiles, 0, sizeof(video_receiver->profiles));
	video_receiver->profiles_count = 0;
	video_receiver->profile_cur = -1;

	video_receiver->frame_index_cur = -1;
	video_receiver->frame_index_prev = -1;
	video_receiver->frame_index_prev_complete = 0;

	chiaki_frame_processor_init(&video_receiver->frame_processor, video_receiver->log);
	video_receiver->packet_stats = packet_stats;

	video_receiver->frames_lost = 0;
	memset(video_receiver->reference_frames, -1, sizeof(video_receiver->reference_frames));
}

CHIAKI_EXPORT void chiaki_video_receiver_fini(ChiakiVideoReceiver *video_receiver)
{
	for(size_t i=0; i<video_receiver->profiles_count; i++)
		free(video_receiver->profiles[i].header);
	chiaki_frame_processor_fini(&video_receiver->frame_processor);
}

CHIAKI_EXPORT void chiaki_video_receiver_stream_info(ChiakiVideoReceiver *video_receiver, ChiakiVideoProfile *profiles, size_t profiles_count)
{
	if(video_receiver->profiles_count > 0)
	{
		CHIAKI_LOGE(video_receiver->log, "Video Receiver profiles already set");
		return;
	}

	memcpy(video_receiver->profiles, profiles, profiles_count * sizeof(ChiakiVideoProfile));
	video_receiver->profiles_count = profiles_count;

	CHIAKI_LOGI(video_receiver->log, "Video Profiles:");
	for(size_t i=0; i<video_receiver->profiles_count; i++)
	{
		ChiakiVideoProfile *profile = &video_receiver->profiles[i];
		CHIAKI_LOGI(video_receiver->log, "  %zu: %ux%u", i, profile->width, profile->height);
		//chiaki_log_hexdump(video_receiver->log, CHIAKI_LOG_DEBUG, profile->header, profile->header_sz);
	}
}

CHIAKI_EXPORT void chiaki_video_receiver_av_packet(ChiakiVideoReceiver *video_receiver, ChiakiTakionAVPacket *packet)
{
	// old frame?
	ChiakiSeqNum16 frame_index = packet->frame_index;
	if(video_receiver->frame_index_cur >= 0
		&& chiaki_seq_num_16_lt(frame_index, (ChiakiSeqNum16)video_receiver->frame_index_cur))
	{
		CHIAKI_LOGW(video_receiver->log, "Video Receiver received old frame packet");
		return;
	}

	// check adaptive stream index
	if(video_receiver->profile_cur < 0 || video_receiver->profile_cur != packet->adaptive_stream_index)
	{
		if(packet->adaptive_stream_index >= video_receiver->profiles_count)
		{
			CHIAKI_LOGE(video_receiver->log, "Packet has invalid adaptive stream index %lu >= %lu",
					(unsigned int)packet->adaptive_stream_index,
					(unsigned int)video_receiver->profiles_count);
			return;
		}
		video_receiver->profile_cur = packet->adaptive_stream_index;

		ChiakiVideoProfile *profile = video_receiver->profiles + video_receiver->profile_cur;
		CHIAKI_LOGI(video_receiver->log, "Switched to profile %d, resolution: %ux%u", video_receiver->profile_cur, profile->width, profile->height);
		if(video_receiver->session->video_sample_cb)
			video_receiver->session->video_sample_cb(profile->header, profile->header_sz, 0, video_receiver->session->video_sample_cb_user);
	}

	// next frame?
	if(video_receiver->frame_index_cur < 0 ||
		chiaki_seq_num_16_gt(frame_index, (ChiakiSeqNum16)video_receiver->frame_index_cur))
	{
		if(video_receiver->packet_stats)
			chiaki_frame_processor_report_packet_stats(&video_receiver->frame_processor, video_receiver->packet_stats);

		// last frame not flushed yet?
		if(video_receiver->frame_index_cur >= 0 && video_receiver->frame_index_prev != video_receiver->frame_index_cur)
			chiaki_video_receiver_flush_frame(video_receiver);

		ChiakiSeqNum16 next_frame_expected = (ChiakiSeqNum16)(video_receiver->frame_index_prev_complete + 1);
		if(chiaki_seq_num_16_gt(frame_index, next_frame_expected)
			&& !(frame_index == 1 && video_receiver->frame_index_cur < 0)) // ok for frame 1
		{
			CHIAKI_LOGW(video_receiver->log, "Detected missing or corrupt frame(s) from %d to %d", next_frame_expected, (int)frame_index);
			stream_connection_send_corrupt_frame(&video_receiver->session->stream_connection, next_frame_expected, frame_index - 1);
		}

		video_receiver->frame_index_cur = frame_index;
		chiaki_frame_processor_alloc_frame(&video_receiver->frame_processor, packet);
	}

	chiaki_frame_processor_put_unit(&video_receiver->frame_processor, packet);

	// if we are currently building up a frame
	if(video_receiver->frame_index_cur != video_receiver->frame_index_prev)
	{
		// if we already have enough for the whole frame, flush it already
		if(chiaki_frame_processor_flush_possible(&video_receiver->frame_processor))
			chiaki_video_receiver_flush_frame(video_receiver);
	}
}

static ChiakiErrorCode chiaki_video_receiver_flush_frame(ChiakiVideoReceiver *video_receiver)
{
	uint8_t *frame;
	size_t frame_size;
	ChiakiFrameProcessorFlushResult flush_result = chiaki_frame_processor_flush(&video_receiver->frame_processor, &frame, &frame_size);

	if(flush_result == CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FAILED
		|| flush_result == CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FEC_FAILED)
	{
		if (flush_result == CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FEC_FAILED)
		{
			ChiakiSeqNum16 next_frame_expected = (ChiakiSeqNum16)(video_receiver->frame_index_prev_complete + 1);
			if(next_frame_expected != video_receiver->frame_index_cur)
				stream_connection_send_corrupt_frame(&video_receiver->session->stream_connection, next_frame_expected, video_receiver->frame_index_cur);
			video_receiver->frames_lost = video_receiver->frame_index_cur - next_frame_expected + 1;
			video_receiver->frame_index_prev = video_receiver->frame_index_cur;
		}
		CHIAKI_LOGW(video_receiver->log, "Failed to complete frame %d", (int)video_receiver->frame_index_cur);
		return CHIAKI_ERR_UNKNOWN;
	}

	bool succ = flush_result != CHIAKI_FRAME_PROCESSOR_FLUSH_RESULT_FEC_FAILED;

	bool parse_succ = false;
	unsigned slice_type, ref_frame;
	if(video_receiver->session->connect_info.video_profile.codec == CHIAKI_CODEC_H264)
		parse_succ = parse_slice_h264(video_receiver, frame, frame_size, &slice_type, &ref_frame);
	else
		parse_succ = parse_slice_h265(video_receiver, frame, frame_size, &slice_type, &ref_frame);
	if(parse_succ)
	{
		if(slice_type == SLICE_TYPE_I)
		{
			add_ref_frame(video_receiver, video_receiver->frame_index_cur);
			CHIAKI_LOGV(video_receiver->log, "Added reference I frame %d", (int)video_receiver->frame_index_cur);
		}
		else if(slice_type == SLICE_TYPE_P)
		{
			int32_t ref_frame_index = video_receiver->frame_index_cur - ref_frame - 1;
			if(ref_frame == 0xff || have_ref_frame(video_receiver, ref_frame_index))
			{
				add_ref_frame(video_receiver, video_receiver->frame_index_cur);
				CHIAKI_LOGV(video_receiver->log, "Added reference P frame %d", (int)video_receiver->frame_index_cur);
			}
			else
			{
				succ = false;
				video_receiver->frames_lost++;
				CHIAKI_LOGW(video_receiver->log, "Missing reference frame %d for decoding frame %d", (int)ref_frame_index, (int)video_receiver->frame_index_cur);
			}
		}
	}

	if(succ && video_receiver->session->video_sample_cb)
	{
		bool cb_succ = video_receiver->session->video_sample_cb(frame, frame_size, video_receiver->frames_lost, video_receiver->session->video_sample_cb_user);
		video_receiver->frames_lost = 0;
		if(!cb_succ)
		{
			succ = false;
			CHIAKI_LOGW(video_receiver->log, "Video callback did not process frame successfully.");
		}
	}

	video_receiver->frame_index_prev = video_receiver->frame_index_cur;

	if(succ)
		video_receiver->frame_index_prev_complete = video_receiver->frame_index_cur;

	return CHIAKI_ERR_SUCCESS;
}
