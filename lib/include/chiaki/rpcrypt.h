// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_RPCRYPT_H
#define CHIAKI_RPCRYPT_H

#include "common.h"

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHIAKI_RPCRYPT_KEY_SIZE 0x10

typedef struct chiaki_rpcrypt_t
{
	ChiakiTarget target;
	uint8_t bright[CHIAKI_RPCRYPT_KEY_SIZE];
	uint8_t ambassador[CHIAKI_RPCRYPT_KEY_SIZE];
} ChiakiRPCrypt;

CHIAKI_EXPORT void chiaki_rpcrypt_bright_ambassador(ChiakiTarget target, uint8_t *bright, uint8_t *ambassador, const uint8_t *nonce, const uint8_t *morning);
CHIAKI_EXPORT void chiaki_rpcrypt_aeropause_ps4_pre10(uint8_t *aeropause, const uint8_t *ambassador);
CHIAKI_EXPORT ChiakiErrorCode chiaki_rpcrypt_aeropause(ChiakiTarget target, size_t key_1_off, uint8_t *aeropause, const uint8_t *ambassador);

CHIAKI_EXPORT void chiaki_rpcrypt_init_auth(ChiakiRPCrypt *rpcrypt, ChiakiTarget target, const uint8_t *nonce, const uint8_t *morning);
CHIAKI_EXPORT void chiaki_rpcrypt_init_regist_ps4_pre10(ChiakiRPCrypt *rpcrypt, const uint8_t *ambassador, uint32_t pin);
CHIAKI_EXPORT ChiakiErrorCode chiaki_rpcrypt_init_regist(ChiakiRPCrypt *rpcrypt, ChiakiTarget target, const uint8_t *ambassador, size_t key_0_off, uint32_t pin);
CHIAKI_EXPORT ChiakiErrorCode chiaki_rpcrypt_generate_iv(ChiakiRPCrypt *rpcrypt, uint8_t *iv, uint64_t counter);
CHIAKI_EXPORT ChiakiErrorCode chiaki_rpcrypt_encrypt(ChiakiRPCrypt *rpcrypt, uint64_t counter, const uint8_t *in, uint8_t *out, size_t sz);
CHIAKI_EXPORT ChiakiErrorCode chiaki_rpcrypt_decrypt(ChiakiRPCrypt *rpcrypt, uint64_t counter, const uint8_t *in, uint8_t *out, size_t sz);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_RPCRYPT_H
