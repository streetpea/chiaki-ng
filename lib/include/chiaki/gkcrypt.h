// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_GKCRYPT_H
#define CHIAKI_GKCRYPT_H

#include "common.h"
#include "log.h"
#include "thread.h"

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHIAKI_GKCRYPT_BLOCK_SIZE 0x10
#define CHIAKI_GKCRYPT_KEY_BUF_BLOCKS_DEFAULT 0x20 // 2MB
#define CHIAKI_GKCRYPT_GMAC_SIZE 4
#define CHIAKI_GKCRYPT_GMAC_KEY_REFRESH_KEY_POS 45000
#define CHIAKI_GKCRYPT_GMAC_KEY_REFRESH_IV_OFFSET 44910

typedef struct chiaki_key_state_t
{
   uint64_t prev;
} ChiakiKeyState;

typedef struct chiaki_gkcrypt_t {
	uint8_t index;

	uint8_t *key_buf; // circular buffer of the ctr mode key stream
	uint64_t key_buf_size;
	uint64_t key_buf_populated; // size of key_buf that is already populated (on startup)
	uint64_t key_buf_key_pos_min; // minimal key pos currently in key_buf
	size_t key_buf_start_offset; // offset in key_buf of the minimal key pos
	uint64_t last_key_pos;        // last key pos that has been requested
	bool key_buf_thread_stop;
	ChiakiMutex key_buf_mutex;
	ChiakiCond key_buf_cond;
	ChiakiThread key_buf_thread;

	uint8_t iv[CHIAKI_GKCRYPT_BLOCK_SIZE];
	uint8_t key_base[CHIAKI_GKCRYPT_BLOCK_SIZE];
	uint8_t key_gmac_base[CHIAKI_GKCRYPT_BLOCK_SIZE];
	uint8_t key_gmac_current[CHIAKI_GKCRYPT_BLOCK_SIZE];
	uint64_t key_gmac_index_current;
	ChiakiLog *log;
} ChiakiGKCrypt;

struct chiaki_session_t;

/**
 * @param key_buf_chunks if > 0, use a thread to generate the ctr mode key stream
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_gkcrypt_init(ChiakiGKCrypt *gkcrypt, ChiakiLog *log, size_t key_buf_chunks, uint8_t index, const uint8_t *handshake_key, const uint8_t *ecdh_secret);

CHIAKI_EXPORT void chiaki_gkcrypt_fini(ChiakiGKCrypt *gkcrypt);
CHIAKI_EXPORT ChiakiErrorCode chiaki_gkcrypt_gen_key_stream(ChiakiGKCrypt *gkcrypt, uint64_t key_pos, uint8_t *buf, size_t buf_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_gkcrypt_get_key_stream(ChiakiGKCrypt *gkcrypt, uint64_t key_pos, uint8_t *buf, size_t buf_size);
CHIAKI_EXPORT ChiakiErrorCode chiaki_gkcrypt_decrypt(ChiakiGKCrypt *gkcrypt, uint64_t key_pos, uint8_t *buf, size_t buf_size);
static inline ChiakiErrorCode chiaki_gkcrypt_encrypt(ChiakiGKCrypt *gkcrypt, uint64_t key_pos, uint8_t *buf, size_t buf_size) { return chiaki_gkcrypt_decrypt(gkcrypt, key_pos, buf, buf_size); }
CHIAKI_EXPORT void chiaki_gkcrypt_gen_gmac_key(uint64_t index, const uint8_t *key_base, const uint8_t *iv, uint8_t *key_out);
CHIAKI_EXPORT void chiaki_gkcrypt_gen_new_gmac_key(ChiakiGKCrypt *gkcrypt, uint64_t index);
CHIAKI_EXPORT void chiaki_gkcrypt_gen_tmp_gmac_key(ChiakiGKCrypt *gkcrypt, uint64_t index, uint8_t *key_out);
CHIAKI_EXPORT ChiakiErrorCode chiaki_gkcrypt_gmac(ChiakiGKCrypt *gkcrypt, uint64_t key_pos, const uint8_t *buf, size_t buf_size, uint8_t *gmac_out);

static inline ChiakiGKCrypt *chiaki_gkcrypt_new(ChiakiLog *log, size_t key_buf_chunks, uint8_t index, const uint8_t *handshake_key, const uint8_t *ecdh_secret)
{
	ChiakiGKCrypt *gkcrypt = CHIAKI_NEW(ChiakiGKCrypt);
	if(!gkcrypt)
		return NULL;
	ChiakiErrorCode err = chiaki_gkcrypt_init(gkcrypt, log, key_buf_chunks, index, handshake_key, ecdh_secret);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		free(gkcrypt);
		return NULL;
	}
	return gkcrypt;
}

static inline void chiaki_gkcrypt_free(ChiakiGKCrypt *gkcrypt)
{
	if(!gkcrypt)
		return;
	chiaki_gkcrypt_fini(gkcrypt);
	free(gkcrypt);
}

CHIAKI_EXPORT void chiaki_key_state_init(ChiakiKeyState *state);

/**
 * @param commit whether to remember this key_pos to update the state. Should only be true after authentication to avoid DoS.
 */
CHIAKI_EXPORT uint64_t chiaki_key_state_request_pos(ChiakiKeyState *state, uint32_t low, bool commit);

/**
 * Update the internal state after knowing that this key_pos is authentic.
 */
CHIAKI_EXPORT void chiaki_key_state_commit(ChiakiKeyState *state, uint64_t prev);

#ifdef __cplusplus
}
#endif

#endif //CHIAKI_GKCRYPT_H
