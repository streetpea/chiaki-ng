// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/bitstream.h>

#include <string.h>

#include "vl_rbsp.h"

static bool skip_startcode(struct vl_vlc *vlc)
{
	vl_vlc_fillbits(vlc);
	for(unsigned i=0; i<64 && vl_vlc_bits_left(vlc)>=32; i++)
	{
		if (vl_vlc_peekbits(vlc, 32) == 1)
			break;
		vl_vlc_eatbits(vlc, 8);
		vl_vlc_fillbits(vlc);
	}
	if(vl_vlc_peekbits(vlc, 32) != 1)
		return false;
	vl_vlc_eatbits(vlc, 32);
	vl_vlc_fillbits(vlc);
	return true;
}

static bool header_h264(ChiakiBitstream *bitstream, uint8_t *data, unsigned size)
{
	struct vl_vlc vlc = {0};
	vl_vlc_init(&vlc, data, size);
	if(!skip_startcode(&vlc))
	{
		CHIAKI_LOGW(bitstream->log, "parse_sps_h264: No startcode found");
		return false;
	}

	vl_vlc_eatbits(&vlc, 1); // forbidden_zero_bit
	vl_vlc_eatbits(&vlc, 2); // nal_ref_idc
	unsigned nal_unit_type = vl_vlc_get_uimsbf(&vlc, 5);

	if(nal_unit_type != 7)
	{
		CHIAKI_LOGW(bitstream->log, "parse_sps_h264: Unexpected NAL unit type %u", nal_unit_type);
		return false;
	}

	struct vl_rbsp rbsp;
	vl_rbsp_init(&rbsp, &vlc, ~0);

	unsigned profile_idc = vl_rbsp_u(&rbsp, 8);

	vl_rbsp_u(&rbsp, 6); // constraint_set_flags
	vl_rbsp_u(&rbsp, 2); // reserved_zero_2bits
	vl_rbsp_u(&rbsp, 8); // level_idc

	vl_rbsp_ue(&rbsp); // seq_parameter_set_id

	if(profile_idc == 100 || profile_idc == 110 ||
		profile_idc == 122 || profile_idc == 244 || profile_idc == 44 ||
		profile_idc == 83 || profile_idc == 86 || profile_idc == 118 ||
		profile_idc == 128 || profile_idc == 138 || profile_idc == 139 ||
		profile_idc == 134 || profile_idc == 135)
	{
		if (vl_rbsp_ue(&rbsp) == 3) // chroma_format_idc
			vl_rbsp_u(&rbsp, 1); // separate_colour_plane_flag

		vl_rbsp_ue(&rbsp); // bit_depth_luma_minus8
		vl_rbsp_ue(&rbsp); // bit_depth_chroma_minus8
		vl_rbsp_u(&rbsp, 1); // qpprime_y_zero_transform_bypass_flag

		if (vl_rbsp_u(&rbsp, 1)) // seq_scaling_matrix_present_flag
			return false;
	}

	bitstream->h264.sps.log2_max_frame_num_minus4 = vl_rbsp_ue(&rbsp);
	if(bitstream->h264.sps.log2_max_frame_num_minus4 > 12)
	{
		CHIAKI_LOGW(bitstream->log, "parse_sps_h264: Unexpected log2_max_frame_num_minus4 value %u", bitstream->h264.sps.log2_max_frame_num_minus4);
		return false;
	}

	return true;
}

static bool header_h265(ChiakiBitstream *bitstream, uint8_t *data, unsigned size)
{
	struct vl_vlc vlc = {0};
	vl_vlc_init(&vlc, data, size);
sps_start:
	if(!skip_startcode(&vlc))
	{
		CHIAKI_LOGW(bitstream->log, "parse_sps_h265: No startcode found");
		return false;
	}

	vl_vlc_eatbits(&vlc, 1); // forbidden_zero_bit
	unsigned nal_unit_type = vl_vlc_get_uimsbf(&vlc, 6);
	vl_vlc_eatbits(&vlc, 6); // nuh_layer_id
	vl_vlc_eatbits(&vlc, 3); // nuh_temporal_id_plus1

	if(nal_unit_type == 32) // VPS
		goto sps_start;

	if(nal_unit_type != 33)
	{
		CHIAKI_LOGW(bitstream->log, "parse_sps_h265: Unexpected NAL unit type %u", nal_unit_type);
		return false;
	}

	struct vl_rbsp rbsp;
	vl_rbsp_init(&rbsp, &vlc, ~0);

	vl_rbsp_u(&rbsp, 4); // sps_video_parameter_set_id
	vl_rbsp_u(&rbsp, 3); // sps_max_sub_layers_minus1
	vl_rbsp_u(&rbsp, 1); // sps_temporal_id_nesting_flag

	vl_rbsp_u(&rbsp, 2); // general_profile_space
	vl_rbsp_u(&rbsp, 1); // general_tier_flag
	vl_rbsp_u(&rbsp, 5); // general_profile_idc
	vl_rbsp_u(&rbsp, 32); // general_profile_compatibility_flag[0-31]
	vl_rbsp_u(&rbsp, 1); // general_progressive_source_flag
	vl_rbsp_u(&rbsp, 1); // general_interlaced_source_flag
	vl_rbsp_u(&rbsp, 1); // general_non_packed_constraint_flag
	vl_rbsp_u(&rbsp, 1); // general_frame_only_constraint_flag
	vl_rbsp_u(&rbsp, 32); vl_rbsp_u(&rbsp, 11); // general_reserved_zero_43bits
	vl_rbsp_u(&rbsp, 1); // general_inbld_flag / general_reserved_zero_bit
	vl_rbsp_u(&rbsp, 8); // general_level_idc

	vl_rbsp_ue(&rbsp); // sps_seq_parameter_set_id
	if(vl_rbsp_ue(&rbsp) == 3) // chroma_format_idc
		vl_rbsp_u(&rbsp, 1); // separate_colour_plane_flag

	vl_rbsp_ue(&rbsp); // pic_width_in_luma_samples
	vl_rbsp_ue(&rbsp); // pic_height_in_luma_samples

	if(vl_rbsp_u(&rbsp, 1)) // conformance_window_flag
	{
		vl_rbsp_ue(&rbsp); // conf_win_left_offset
		vl_rbsp_ue(&rbsp); // conf_win_right_offset
		vl_rbsp_ue(&rbsp); // conf_win_top_offset
		vl_rbsp_ue(&rbsp); // conf_win_bottom_offset
	}

	vl_rbsp_ue(&rbsp); // bit_depth_luma_minus8
	vl_rbsp_ue(&rbsp); // bit_depth_chroma_minus8

	bitstream->h265.sps.log2_max_pic_order_cnt_lsb_minus4 = vl_rbsp_ue(&rbsp);
	if(bitstream->h265.sps.log2_max_pic_order_cnt_lsb_minus4 > 12)
	{
		CHIAKI_LOGW(bitstream->log, "parse_sps_h265: Unexpected log2_max_pic_order_cnt_lsb_minus4 value %u", bitstream->h265.sps.log2_max_pic_order_cnt_lsb_minus4);
		return false;
	}

	return true;
}

static bool slice_h264(ChiakiBitstream *bitstream, uint8_t *data, unsigned size, ChiakiBitstreamSlice *slice)
{
	struct vl_vlc vlc = {0};
	vl_vlc_init(&vlc, data, size);
	if(!skip_startcode(&vlc))
	{
		CHIAKI_LOGW(bitstream->log, "parse_slice_h264: No startcode found");
		return false;
	}

	vl_vlc_eatbits(&vlc, 1); // forbidden_zero_bit
	vl_vlc_eatbits(&vlc, 2); // nal_ref_idc
	unsigned nal_unit_type = vl_vlc_get_uimsbf(&vlc, 5);

	if(nal_unit_type != 1 && nal_unit_type != 5)
	{
		CHIAKI_LOGW(bitstream->log, "parse_slice_h264: Unexpected NAL unit type %u", nal_unit_type);
		return false;
	}

	struct vl_rbsp rbsp;
	vl_rbsp_init(&rbsp, &vlc, ~0);
	vl_rbsp_ue(&rbsp); // first_mb_in_slice

	switch(vl_rbsp_ue(&rbsp))
	{
		case 0:
		case 5:
			slice->slice_type = CHIAKI_BITSTREAM_SLICE_P;
			break;
		case 2:
		case 7:
			slice->slice_type = CHIAKI_BITSTREAM_SLICE_I;
			break;
		default:
			slice->slice_type = CHIAKI_BITSTREAM_SLICE_UNKNOWN;
			break;
	}

	if(nal_unit_type == 1)
	{
		slice->reference_frame = 0;
		vl_rbsp_ue(&rbsp); // pic_parameter_set_id
		vl_rbsp_u(&rbsp, bitstream->h264.sps.log2_max_frame_num_minus4 + 4); // frame_num
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
					slice->reference_frame = vl_rbsp_ue(&rbsp); // abs_diff_pic_num_minus1
				else if(modification_of_pic_nums_idc < 3)
					vl_rbsp_ue(&rbsp); // abs_diff_pic_num_minus1 or long_term_pic_num
				else if(modification_of_pic_nums_idc == 3)
					return true;
				else
					break;
				modification_of_pic_nums_idc = vl_rbsp_ue(&rbsp);
			}
			CHIAKI_LOGW(bitstream->log, "parse_slice_h264: Failed to parse ref_pic_list_modification");
			return false;
		}
	}

	return true;
}

static bool slice_h265(ChiakiBitstream *bitstream, uint8_t *data, unsigned size, ChiakiBitstreamSlice *slice)
{
	struct vl_vlc vlc = {0};
	vl_vlc_init(&vlc, data, size);

	if(!skip_startcode(&vlc))
	{
		CHIAKI_LOGW(bitstream->log, "parse_slice_h265: No startcode found");
		return false;
	}

	vl_vlc_eatbits(&vlc, 1); // forbidden_zero_bit
	unsigned nal_unit_type = vl_vlc_get_uimsbf(&vlc, 6);
	vl_vlc_eatbits(&vlc, 6); // nuh_layer_id
	vl_vlc_eatbits(&vlc, 3); // nuh_temporal_id_plus1

	if(nal_unit_type != 1 && nal_unit_type != 20)
	{
		CHIAKI_LOGW(bitstream->log, "parse_slice_h265: Unexpected NAL unit type %u", nal_unit_type);
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
			slice->slice_type = CHIAKI_BITSTREAM_SLICE_P;
			break;
		case 2:
			slice->slice_type = CHIAKI_BITSTREAM_SLICE_I;
			break;
		default:
			slice->slice_type = CHIAKI_BITSTREAM_SLICE_UNKNOWN;
			break;
	}

	if(nal_unit_type == 1)
	{
		slice->reference_frame = 0xff;
		vl_rbsp_u(&rbsp, bitstream->h265.sps.log2_max_pic_order_cnt_lsb_minus4 + 4); // slice_pic_order_cnt_lsb
		if(!vl_rbsp_u(&rbsp, 1)) // short_term_ref_pic_set_sps_flag
		{
			unsigned num_negative_pics = vl_rbsp_ue(&rbsp);
			if(num_negative_pics > 16)
			{
				CHIAKI_LOGW(bitstream->log, "parse_slice_h265: Unexpected num_negative_pics %u", num_negative_pics);
				return false;
			}
			vl_rbsp_ue(&rbsp); // num_positive_pics
			for(unsigned i=0; i<num_negative_pics; i++)
			{
				vl_rbsp_ue(&rbsp); // delta_poc_s0_minus1[i]
				if(vl_rbsp_u(&rbsp, 1)) // used_by_curr_pic_s0_flag[i]
				{
					slice->reference_frame = i;
					break;
				}
			}
		}
		if(slice->reference_frame == 0xff)
			CHIAKI_LOGV(bitstream->log, "parse_slice_h265: No ref frame found");
	}

	return true;
}

static bool slice_set_reference_frame_h265(ChiakiBitstream *bitstream, uint8_t *data, unsigned size, unsigned reference_frame)
{
	struct vl_vlc vlc = {0};
	vl_vlc_init(&vlc, data, size);
	if(!skip_startcode(&vlc))
	{
		CHIAKI_LOGW(bitstream->log, "slice_set_reference_frame_h265: No startcode found");
		return false;
	}

	vl_vlc_eatbits(&vlc, 1); // forbidden_zero_bit
	unsigned nal_unit_type = vl_vlc_get_uimsbf(&vlc, 6);
	vl_vlc_eatbits(&vlc, 6); // nuh_layer_id
	vl_vlc_eatbits(&vlc, 3); // nuh_temporal_id_plus1

	if(nal_unit_type != 1)
	{
		CHIAKI_LOGW(bitstream->log, "slice_set_reference_frame_h265: Unexpected NAL unit type %u", nal_unit_type);
		return false;
	}

	struct vl_rbsp rbsp;
	vl_rbsp_init(&rbsp, &vlc, ~0);
	unsigned first_slice_segment_in_pic_flag = vl_rbsp_u(&rbsp, 1);

	vl_rbsp_ue(&rbsp); // slice_pic_parameter_set_id
	if(!first_slice_segment_in_pic_flag)
		vl_rbsp_ue(&rbsp); // slice_segment_address

	if(vl_rbsp_ue(&rbsp) != 1)
	{
		CHIAKI_LOGW(bitstream->log, "slice_set_reference_frame_h265: Not P slice");
		return false;
	}

	vl_rbsp_u(&rbsp, bitstream->h265.sps.log2_max_pic_order_cnt_lsb_minus4 + 4); // slice_pic_order_cnt_lsb
	if(!vl_rbsp_u(&rbsp, 1)) // short_term_ref_pic_set_sps_flag
	{
		unsigned num_negative_pics = vl_rbsp_ue(&rbsp);
		if(num_negative_pics > 16)
		{
			CHIAKI_LOGW(bitstream->log, "slice_set_reference_frame_h265: Unexpected num_negative_pics %u", num_negative_pics);
			return false;
		}
		vl_rbsp_ue(&rbsp); // num_positive_pics
		for(unsigned i=0; i<num_negative_pics; i++)
		{
			vl_rbsp_ue(&rbsp); // delta_poc_s0_minus1[i]
			uint32_t *d = (uint32_t*)rbsp.nal.data;
			uint64_t hi = ntohl(d[-2]);
			uint64_t lo = ntohl(d[-1]);
			uint64_t buffer = lo | (hi << 32);
			uint64_t mask = (uint64_t)1 << (64 - 1 - (64 - (32 - rbsp.nal.invalid_bits)));
			if (i == reference_frame)
				buffer |= mask; // set 1
			else
				buffer &= ~mask; // set 0
			hi = htonl(buffer >> 32);
			lo = htonl(buffer & 0xffffffff);
			d[-2] = hi;
			d[-1] = lo;
			if (i == reference_frame)
				return true;
			vl_rbsp_u(&rbsp, 1); // used_by_curr_pic_s0_flag[i]
		}
	}

	return false;
}

void chiaki_bitstream_init(ChiakiBitstream *bitstream, ChiakiLog *log, ChiakiCodec codec)
{
	bitstream->log = log;
	bitstream->codec = codec;
}

bool chiaki_bitstream_header(ChiakiBitstream *bitstream, uint8_t *data, unsigned size)
{
	if(bitstream->codec == CHIAKI_CODEC_H264)
	{
		memset(&bitstream->h264, 0, sizeof(bitstream->h264));
		return header_h264(bitstream, data, size);
	}
	else
	{
		memset(&bitstream->h265, 0, sizeof(bitstream->h265));
		return header_h265(bitstream, data, size);
	}
}

bool chiaki_bitstream_slice(ChiakiBitstream *bitstream, uint8_t *data, unsigned size, ChiakiBitstreamSlice *slice)
{
	if(bitstream->codec == CHIAKI_CODEC_H264)
		return slice_h264(bitstream, data, size, slice);
	else
		return slice_h265(bitstream, data, size, slice);
}

bool chiaki_bitstream_slice_set_reference_frame(ChiakiBitstream *bitstream, uint8_t *data, unsigned size, unsigned reference_frame)
{
	if(bitstream->codec == CHIAKI_CODEC_H264)
		return false;
	else
		return slice_set_reference_frame_h265(bitstream, data, size, reference_frame);
}
