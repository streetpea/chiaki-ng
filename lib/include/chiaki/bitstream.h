// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_BITSTREAM_H
#define CHIAKI_BITSTREAM_H

#include <stdint.h>

#include "common.h"
#include "log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct chiaki_bitstream_t
{
	ChiakiLog *log;
	ChiakiCodec codec;
	union
	{
		struct
		{
			struct
			{
				uint32_t log2_max_frame_num_minus4;
			} sps;
		} h264;

		struct
		{
			struct
			{
				uint32_t log2_max_pic_order_cnt_lsb_minus4;
			} sps;
		} h265;
	};
} ChiakiBitstream;

typedef enum chiaki_bitstream_slice_type_t
{
	CHIAKI_BITSTREAM_SLICE_UNKNOWN = 0,
	CHIAKI_BITSTREAM_SLICE_I,
	CHIAKI_BITSTREAM_SLICE_P,
} ChiakiBitstreamSliceType;

typedef struct chiaki_bitstream_slice_t
{
	ChiakiBitstreamSliceType slice_type;
	unsigned reference_frame;
} ChiakiBitstreamSlice;

CHIAKI_EXPORT void chiaki_bitstream_init(ChiakiBitstream *bitstream, ChiakiLog *log, ChiakiCodec codec);
CHIAKI_EXPORT bool chiaki_bitstream_header(ChiakiBitstream *bitstream, uint8_t *data, unsigned size);
CHIAKI_EXPORT bool chiaki_bitstream_slice(ChiakiBitstream *bitstream, uint8_t *data, unsigned size, ChiakiBitstreamSlice *slice);
CHIAKI_EXPORT bool chiaki_bitstream_slice_set_reference_frame(ChiakiBitstream *bitstream, uint8_t *data, unsigned size, unsigned reference_frame);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_BITSTREAM_H
