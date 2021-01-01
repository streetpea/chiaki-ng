// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/base64.h>

#include <stdint.h>

// Implementations taken from
// https://en.wikibooks.org/wiki/Algorithm_Implementation/Miscellaneous/Base64

CHIAKI_EXPORT ChiakiErrorCode chiaki_base64_encode(const uint8_t *in, size_t in_size, char *out, size_t out_size)
{
	const char base64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t result_index = 0;
	size_t x;
	uint32_t n = 0;
	size_t pad_count = in_size % 3;
	uint8_t n0, n1, n2, n3;

	// increment over the length of the string, three characters at a time
	for(x = 0; x < in_size; x += 3)
	{
		// these three 8-bit (ASCII) characters become one 24-bit number
		n = ((uint32_t)in[x]) << 16;

		if((x + 1) < in_size)
			n += ((uint32_t)in[x + 1]) << 8;

		if((x + 2) < in_size)
			n += in[x + 2];

		// this 24-bit number gets separated into four 6-bit numbers
		n0 = (uint8_t)(n >> 18) & 63;
		n1 = (uint8_t)(n >> 12) & 63;
		n2 = (uint8_t)(n >> 6) & 63;
		n3 = (uint8_t)n & 63;

		// if we have one byte available, then its encoding is spread
		// out over two characters
		if(result_index >= out_size)
			return CHIAKI_ERR_BUF_TOO_SMALL;
		out[result_index++] = base64chars[n0];
		if(result_index >= out_size)
			return CHIAKI_ERR_BUF_TOO_SMALL;
		out[result_index++] = base64chars[n1];

		// if we have only two bytes available, then their encoding is
		// spread out over three chars
		if((x + 1) < in_size)
		{
			if(result_index >= out_size)
				return CHIAKI_ERR_BUF_TOO_SMALL;
			out[result_index++] = base64chars[n2];
		}

		// if we have all three bytes available, then their encoding is spread
		// out over four characters
		if((x + 2) < in_size)
		{
			if(result_index >= out_size)
				return CHIAKI_ERR_BUF_TOO_SMALL;
			out[result_index++] = base64chars[n3];
		}
	}

	// create and add padding that is required if we did not have a multiple of 3
	// number of characters available
	if(pad_count > 0)
	{
		for(; pad_count < 3; pad_count++)
		{
			if(result_index >= out_size)
				return CHIAKI_ERR_BUF_TOO_SMALL;
			out[result_index++] = '=';
		}
	}
	if(result_index >= out_size)
		return CHIAKI_ERR_BUF_TOO_SMALL;
	out[result_index] = 0;
	return CHIAKI_ERR_SUCCESS;
}

#define WHITESPACE 64
#define EQUALS	   65
#define INVALID	   66

static const unsigned char d[] = {
	66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 64, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66,
	66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 62, 66, 66, 66, 63, 52, 53,
	54, 55, 56, 57, 58, 59, 60, 61, 66, 66, 66, 65, 66, 66, 66, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
	10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 66, 66, 66, 66, 66, 66, 26, 27, 28,
	29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 66, 66,
	66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66,
	66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66,
	66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66,
	66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66,
	66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66,
	66, 66, 66, 66, 66, 66
};

CHIAKI_EXPORT ChiakiErrorCode chiaki_base64_decode(const char *in, size_t in_size, uint8_t *out, size_t *out_size)
{
	const char *end = in + in_size;
	char iter = 0;
	uint32_t buf = 0;
	size_t len = 0;

	while(in < end)
	{
		unsigned char c = d[(size_t)(*in++)];

		switch(c)
		{
			case WHITESPACE:
				continue; // skip whitespace
			case INVALID:
				return CHIAKI_ERR_INVALID_DATA; // invalid input
			case EQUALS:						// pad character, end of data
				in = end;
				continue;
			default:
				buf = buf << 6 | c;
				iter++; // increment the number of iteration
				// If the buffer is full, split it into bytes
				if(iter == 4)
				{
					if((len += 3) > *out_size)
						return CHIAKI_ERR_BUF_TOO_SMALL;
					*(out++) = (unsigned char)((buf >> 16) & 0xff);
					*(out++) = (unsigned char)((buf >> 8) & 0xff);
					*(out++) = (unsigned char)(buf & 0xff);
					buf = 0;
					iter = 0;
				}
		}
	}

	if(iter == 3)
	{
		if((len += 2) > *out_size)
			return CHIAKI_ERR_BUF_TOO_SMALL;
		*(out++) = (unsigned char)((buf >> 10) & 0xff);
		*(out++) = (unsigned char)((buf >> 2) & 0xff);
	}
	else if(iter == 2)
	{
		if(++len > *out_size)
			return CHIAKI_ERR_BUF_TOO_SMALL;
		*(out++) = (unsigned char)((buf >> 4) & 0xff);
	}

	*out_size = len;
	return CHIAKI_ERR_SUCCESS;
}
