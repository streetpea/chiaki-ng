// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_COMMON_H
#define CHIAKI_COMMON_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __GNUC__
typedef uint32_t chiaki_unaligned_uint32_t __attribute__((aligned(1)));
typedef uint16_t chiaki_unaligned_uint16_t __attribute__((aligned(1)));
#else
typedef uint32_t chiaki_unaligned_uint32_t;
typedef uint16_t chiaki_unaligned_uint16_t;
#endif

#define CHIAKI_EXPORT

#define CHIAKI_NEW(t) ((t*)malloc(sizeof(t)))

typedef enum
{
	CHIAKI_ERR_SUCCESS = 0,
	CHIAKI_ERR_UNKNOWN,
	CHIAKI_ERR_PARSE_ADDR,
	CHIAKI_ERR_THREAD,
	CHIAKI_ERR_MEMORY,
	CHIAKI_ERR_OVERFLOW,
	CHIAKI_ERR_NETWORK,
	CHIAKI_ERR_CONNECTION_REFUSED,
	CHIAKI_ERR_HOST_DOWN,
	CHIAKI_ERR_HOST_UNREACH,
	CHIAKI_ERR_DISCONNECTED,
	CHIAKI_ERR_INVALID_DATA,
	CHIAKI_ERR_BUF_TOO_SMALL,
	CHIAKI_ERR_MUTEX_LOCKED,
	CHIAKI_ERR_CANCELED,
	CHIAKI_ERR_TIMEOUT,
	CHIAKI_ERR_INVALID_RESPONSE,
	CHIAKI_ERR_INVALID_MAC,
	CHIAKI_ERR_UNINITIALIZED,
	CHIAKI_ERR_FEC_FAILED
} ChiakiErrorCode;

CHIAKI_EXPORT const char *chiaki_error_string(ChiakiErrorCode code);

CHIAKI_EXPORT void *chiaki_aligned_alloc(size_t alignment, size_t size);
CHIAKI_EXPORT void chiaki_aligned_free(void *ptr);

typedef enum
{
	// values must not change
	CHIAKI_TARGET_PS4_UNKNOWN =       0,
	CHIAKI_TARGET_PS4_8 =           800,
	CHIAKI_TARGET_PS4_9 =           900,
	CHIAKI_TARGET_PS4_10 =         1000,
	CHIAKI_TARGET_PS5_UNKNOWN = 1000000,
	CHIAKI_TARGET_PS5_1 =       1000100
} ChiakiTarget;

static inline bool chiaki_target_is_unknown(ChiakiTarget target)
{
	return target == CHIAKI_TARGET_PS5_UNKNOWN
		|| target == CHIAKI_TARGET_PS4_UNKNOWN;
}

static inline bool chiaki_target_is_ps5(ChiakiTarget target) { return target >= CHIAKI_TARGET_PS5_UNKNOWN; }

/**
 * Perform initialization of global state needed for using the Chiaki lib
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_lib_init();

typedef enum
{
	CHIAKI_CODEC_H264 = 0,
	CHIAKI_CODEC_H265,
	CHIAKI_CODEC_H265_HDR
} ChiakiCodec;

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_COMMON_H
