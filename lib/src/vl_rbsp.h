/**************************************************************************
 *
 * Copyright 2013 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

/*
 * Authors:
 *      Christian König <christian.koenig@amd.com>
 *
 */

#ifndef vl_rbsp_h
#define vl_rbsp_h

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

struct vl_vlc
{
   uint64_t buffer;
   signed invalid_bits;
   const uint8_t *data;
   const uint8_t *end;

   const void *input;
   unsigned   size;
   unsigned   bytes_left;
};

/**
 * align the data pointer to the next dword
 */
static inline void
vl_vlc_align_data_ptr(struct vl_vlc *vlc)
{
   /* align the data pointer */
   while (vlc->data != vlc->end && ((uintptr_t)vlc->data) & 3) {
      vlc->buffer |= (uint64_t)*vlc->data << (24 + vlc->invalid_bits);
      ++vlc->data;
      vlc->invalid_bits -= 8;
   }
}

/**
 * fill the bit buffer, so that at least 32 bits are valid
 */
static inline void
vl_vlc_fillbits(struct vl_vlc *vlc)
{
   assert(vlc);

   /* as long as the buffer needs to be filled */
   while (vlc->invalid_bits > 0) {
      unsigned bytes_left = vlc->end - vlc->data;

      /* if this input is depleted */
      if (bytes_left == 0)
          return;

      if (bytes_left >= 4) {

         /* enough bytes in buffer, read in a whole dword */
         uint64_t value = ntohl(*(const uint32_t*)vlc->data);

         vlc->buffer |= value << vlc->invalid_bits;
         vlc->data += 4;
         vlc->invalid_bits -= 32;

         /* buffer is now definitely filled up avoid the loop test */
         break;

      } else while (vlc->data < vlc->end) {

         /* not enough bytes left in buffer, read single bytes */
         vlc->buffer |= (uint64_t)*vlc->data << (24 + vlc->invalid_bits);
         ++vlc->data;
         vlc->invalid_bits -= 8;
      }
   }
}

/**
 * initialize vlc structure and start reading from first input buffer
 */
static inline void
vl_vlc_init(struct vl_vlc *vlc, const void *input, unsigned size)
{
   assert(vlc);

   vlc->buffer = 0;
   vlc->invalid_bits = 32;
   vlc->input = input;
   vlc->size = size;
   vlc->bytes_left = vlc->size;
   vlc->data = input;
   vlc->end = vlc->data + size;

   vl_vlc_align_data_ptr(vlc);
   vl_vlc_fillbits(vlc);
}

/**
 * number of bits still valid in bit buffer
 */
static inline unsigned
vl_vlc_valid_bits(struct vl_vlc *vlc)
{
   return 32 - vlc->invalid_bits;
}

/**
 * number of bits left over all inbut buffers
 */
static inline unsigned
vl_vlc_bits_left(struct vl_vlc *vlc)
{
   signed bytes_left = vlc->end - vlc->data;
   bytes_left += vlc->bytes_left;
   return bytes_left * 8 + vl_vlc_valid_bits(vlc);
}

/**
 * get num_bits from bit buffer without removing them
 */
static inline unsigned
vl_vlc_peekbits(struct vl_vlc *vlc, unsigned num_bits)
{
   assert(vl_vlc_valid_bits(vlc) >= num_bits || vlc->data >= vlc->end);
   return vlc->buffer >> (64 - num_bits);
}

/**
 * remove num_bits from bit buffer
 */
static inline void
vl_vlc_eatbits(struct vl_vlc *vlc, unsigned num_bits)
{
   assert(vl_vlc_valid_bits(vlc) >= num_bits);

   vlc->buffer <<= num_bits;
   vlc->invalid_bits += num_bits;
}

/**
 * get num_bits from bit buffer with removing them
 */
static inline unsigned
vl_vlc_get_uimsbf(struct vl_vlc *vlc, unsigned num_bits)
{
   unsigned value;

   assert(vl_vlc_valid_bits(vlc) >= num_bits);

   value = vlc->buffer >> (64 - num_bits);
   vl_vlc_eatbits(vlc, num_bits);

   return value;
}

/**
 * treat num_bits as signed value and remove them from bit buffer
 */
static inline signed
vl_vlc_get_simsbf(struct vl_vlc *vlc, unsigned num_bits)
{
   signed value;

   assert(vl_vlc_valid_bits(vlc) >= num_bits);

   value = ((int64_t)vlc->buffer) >> (64 - num_bits);
   vl_vlc_eatbits(vlc, num_bits);

   return value;
}

/**
 * fast forward search for a specific byte value
 */
static inline bool
vl_vlc_search_byte(struct vl_vlc *vlc, unsigned num_bits, uint8_t value)
{
   /* make sure we are on a byte boundary */
   assert((vl_vlc_valid_bits(vlc) % 8) == 0);
   assert(num_bits == ~0u || (num_bits % 8) == 0);

   /* deplete the bit buffer */
   while (vl_vlc_valid_bits(vlc) > 0) {

      if (vl_vlc_peekbits(vlc, 8) == value) {
         vl_vlc_fillbits(vlc);
         return true;
      }

      vl_vlc_eatbits(vlc, 8);

      if (num_bits != ~0u) {
         num_bits -= 8;
         if (num_bits == 0)
            return false;
      }
   }

   /* deplete the byte buffers */
   while (1) {

      /* if this input is depleted */
      if (vlc->data == vlc->end)
          return false;

      if (*vlc->data == value) {
         vl_vlc_align_data_ptr(vlc);
         vl_vlc_fillbits(vlc);
         return true;
      }

      ++vlc->data;
      if (num_bits != ~0u) {
         num_bits -= 8;
         if (num_bits == 0) {
            vl_vlc_align_data_ptr(vlc);
            return false;
         }
      }
   }
}

/**
 * remove num_bits bits starting at pos from the bitbuffer
 */
static inline void
vl_vlc_removebits(struct vl_vlc *vlc, unsigned pos, unsigned num_bits)
{
   uint64_t lo = (vlc->buffer & (UINT64_MAX >> (pos + num_bits))) << num_bits;
   uint64_t hi = (vlc->buffer & (UINT64_MAX << (64 - pos)));
   vlc->buffer = lo | hi;
   vlc->invalid_bits += num_bits;
}

/**
 * limit the number of bits left for fetching
 */
static inline void
vl_vlc_limit(struct vl_vlc *vlc, unsigned bits_left)
{
   assert(bits_left <= vl_vlc_bits_left(vlc));

   vl_vlc_fillbits(vlc);
   if (bits_left < vl_vlc_valid_bits(vlc)) {
      vlc->invalid_bits = 32 - bits_left;
      vlc->buffer &= ~0L << (vlc->invalid_bits + 32);
      vlc->end = vlc->data;
      vlc->bytes_left = 0;
   } else {
      assert((bits_left - vl_vlc_valid_bits(vlc)) % 8 == 0);
      vlc->bytes_left = (bits_left - vl_vlc_valid_bits(vlc)) / 8;
      if (vlc->bytes_left < (vlc->end - vlc->data)) {
         vlc->end = vlc->data + vlc->bytes_left;
         vlc->bytes_left = 0;
      } else
         vlc->bytes_left -= vlc->end - vlc->data;
   }
}

struct vl_rbsp {
   struct vl_vlc nal;
   unsigned escaped;
   unsigned removed;
};

/**
 * Initialize the RBSP object
 */
static inline void vl_rbsp_init(struct vl_rbsp *rbsp, struct vl_vlc *nal, unsigned num_bits)
{
   unsigned valid, bits_left = vl_vlc_bits_left(nal);
   int i;

   /* copy the position */
   rbsp->nal = *nal;

   rbsp->escaped = 0;
   rbsp->removed = 0;

   /* search for the end of the NAL unit */
   while (vl_vlc_search_byte(nal, num_bits, 0x00)) {
      if (vl_vlc_peekbits(nal, 24) == 0x000001 ||
          vl_vlc_peekbits(nal, 32) == 0x00000001) {
         vl_vlc_limit(&rbsp->nal, bits_left - vl_vlc_bits_left(nal));
         break;
      }
      vl_vlc_eatbits(nal, 8);
   }

   valid = vl_vlc_valid_bits(&rbsp->nal);
   /* search for the emulation prevention three byte */
   for (i = 24; i <= valid; i += 8) {
      if ((vl_vlc_peekbits(&rbsp->nal, i) & 0xffffff) == 0x3) {
         vl_vlc_removebits(&rbsp->nal, i - 8, 8);
         i += 8;
      }
   }

   valid = vl_vlc_valid_bits(&rbsp->nal);

   rbsp->escaped = (valid >= 16) ? 16 : ((valid >= 8) ? 8 : 0);
}

/**
 * Make at least 16 more bits available
 */
static inline void vl_rbsp_fillbits(struct vl_rbsp *rbsp)
{
   unsigned valid = vl_vlc_valid_bits(&rbsp->nal);
   unsigned i, bits;

   /* abort if we still have enough bits */
   if (valid >= 32)
      return;

   vl_vlc_fillbits(&rbsp->nal);

   /* abort if we have less than 24 bits left in this nal */
   if (vl_vlc_bits_left(&rbsp->nal) < 24)
      return;

   /* handle the already escaped bits */
   valid -= rbsp->escaped;

   /* search for the emulation prevention three byte */
   rbsp->escaped = 16;
   bits = vl_vlc_valid_bits(&rbsp->nal);
   for (i = valid + 24; i <= bits; i += 8) {
      if ((vl_vlc_peekbits(&rbsp->nal, i) & 0xffffff) == 0x3) {
         vl_vlc_removebits(&rbsp->nal, i - 8, 8);
         rbsp->escaped = bits - i;
         bits -= 8;
         rbsp->removed += 8;
         i += 8;
      }
   }
}

/**
 * Return an unsigned integer from the first n bits
 */
static inline unsigned vl_rbsp_u(struct vl_rbsp *rbsp, unsigned n)
{
   if (n == 0)
      return 0;

   vl_rbsp_fillbits(rbsp);
   if (n > 16)
      vl_rbsp_fillbits(rbsp);
   return vl_vlc_get_uimsbf(&rbsp->nal, n);
}

/**
 * Return an unsigned exponential Golomb encoded integer
 */
static inline unsigned vl_rbsp_ue(struct vl_rbsp *rbsp)
{
   unsigned bits = 0;

   vl_rbsp_fillbits(rbsp);
   while (!vl_vlc_get_uimsbf(&rbsp->nal, 1))
      ++bits;

   return (1 << bits) - 1 + vl_rbsp_u(rbsp, bits);
}

/**
 * Return an signed exponential Golomb encoded integer
 */
static inline signed vl_rbsp_se(struct vl_rbsp *rbsp)
{
   signed codeNum = vl_rbsp_ue(rbsp);
   if (codeNum & 1)
      return (codeNum + 1) >> 1;
   else
      return -(codeNum >> 1);
}

/**
 * Are more data available in the RBSP ?
 */
static inline bool vl_rbsp_more_data(struct vl_rbsp *rbsp)
{
   unsigned bits, value;

   if (vl_vlc_bits_left(&rbsp->nal) > 8)
      return true;

   bits = vl_vlc_valid_bits(&rbsp->nal);
   value = vl_vlc_peekbits(&rbsp->nal, bits);
   if (value == 0 || value == (1 << (bits - 1)))
      return false;

   return true;
}

#endif /* vl_rbsp_h */
