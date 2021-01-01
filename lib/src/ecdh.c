// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/session.h>
#include <chiaki/ecdh.h>
#include <chiaki/base64.h>

#ifdef CHIAKI_LIB_ENABLE_MBEDTLS
#include "mbedtls/entropy.h"
#include "mbedtls/md.h"
#else
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/hmac.h>
#include <openssl/bn.h>
#include <openssl/ecdh.h>
#endif

// memset
#include <string.h>

#include <stdio.h>

CHIAKI_EXPORT ChiakiErrorCode chiaki_ecdh_init(ChiakiECDH *ecdh)
{
	memset(ecdh, 0, sizeof(ChiakiECDH));
#ifdef CHIAKI_LIB_ENABLE_MBEDTLS
#define CHECK(err) if((err) != 0) { \
	chiaki_ecdh_fini(ecdh); \
	return CHIAKI_ERR_UNKNOWN; }
	// mbedtls ecdh example:
	// https://github.com/ARMmbed/mbedtls/blob/development/programs/pkey/ecdh_curve25519.c
	const char pers[] = "ecdh";
	mbedtls_entropy_context entropy;
	//init RNG Seed context
	mbedtls_entropy_init(&entropy);
	// init local key
	//mbedtls_ecp_keypair_init(&ecdh->key_local);
	mbedtls_ecdh_init(&ecdh->ctx);
	// init ecdh group
	// keep rng context in ecdh for later reuse
	mbedtls_ctr_drbg_init(&ecdh->drbg);

	// build RNG seed
	CHECK(mbedtls_ctr_drbg_seed(&ecdh->drbg, mbedtls_entropy_func, &entropy,
		(const unsigned char *) pers, sizeof pers));

	// build MBEDTLS_ECP_DP_SECP256K1 group
	CHECK(mbedtls_ecp_group_load(&ecdh->ctx.grp, MBEDTLS_ECP_DP_SECP256K1));
	// build key
	CHECK(mbedtls_ecdh_gen_public(&ecdh->ctx.grp, &ecdh->ctx.d,
		&ecdh->ctx.Q, mbedtls_ctr_drbg_random, &ecdh->drbg));

	// relese entropy ptr
	mbedtls_entropy_free(&entropy);
#undef CHECK

#else
#define CHECK(a) if(!(a)) { chiaki_ecdh_fini(ecdh); return CHIAKI_ERR_UNKNOWN; }
	CHECK(ecdh->group = EC_GROUP_new_by_curve_name(NID_secp256k1));

	CHECK(ecdh->key_local = EC_KEY_new());
	CHECK(EC_KEY_set_group(ecdh->key_local, ecdh->group));
	CHECK(EC_KEY_generate_key(ecdh->key_local));

#undef CHECK
#endif

	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT void chiaki_ecdh_fini(ChiakiECDH *ecdh)
{
#ifdef CHIAKI_LIB_ENABLE_MBEDTLS
	mbedtls_ecdh_free(&ecdh->ctx);
	mbedtls_ctr_drbg_free(&ecdh->drbg);
#else
	EC_KEY_free(ecdh->key_local);
	EC_GROUP_free(ecdh->group);
#endif
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_ecdh_set_local_key(ChiakiECDH *ecdh, const uint8_t *private_key, size_t private_key_size, const uint8_t *public_key, size_t public_key_size)
{
#ifdef CHIAKI_LIB_ENABLE_MBEDTLS
	//https://tls.mbed.org/discussions/generic/publickey-binary-data-in-der
	// Load keys from buffers (i.e: config file)
	// TODO test

	// public
	int r = 0;
	r = mbedtls_ecp_point_read_binary(&ecdh->ctx.grp, &ecdh->ctx.Q, public_key, public_key_size);
	if(r != 0)
		return CHIAKI_ERR_UNKNOWN;

	// secret
	r = mbedtls_mpi_read_binary(&ecdh->ctx.d, private_key, private_key_size);
	if(r != 0)
		return CHIAKI_ERR_UNKNOWN;

	// regen key
	r = mbedtls_ecdh_gen_public(&ecdh->ctx.grp, &ecdh->ctx.d, &ecdh->ctx.Q, mbedtls_ctr_drbg_random, &ecdh->drbg);
	if(r != 0)
		return CHIAKI_ERR_UNKNOWN;

	return CHIAKI_ERR_SUCCESS;
#else
	ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;

	BIGNUM *private_key_bn = BN_bin2bn(private_key, (int)private_key_size, NULL);
	if(!private_key_bn)
		return CHIAKI_ERR_UNKNOWN;

	EC_POINT *public_key_point = EC_POINT_new(ecdh->group);
	if(!public_key_point)
	{
		err = CHIAKI_ERR_UNKNOWN;
		goto error_priv;
	}

	if(!EC_POINT_oct2point(ecdh->group, public_key_point, public_key, public_key_size, NULL))
	{
		err = CHIAKI_ERR_UNKNOWN;
		goto error_pub;
	}

	if(!EC_KEY_set_private_key(ecdh->key_local, private_key_bn))
	{
		err = CHIAKI_ERR_UNKNOWN;
		goto error_pub;
	}

	if(!EC_KEY_set_public_key(ecdh->key_local, public_key_point))
	{
		err = CHIAKI_ERR_UNKNOWN;
		goto error_pub;
	}

error_pub:
	EC_POINT_free(public_key_point);
error_priv:
	BN_free(private_key_bn);
	return err;
#endif
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_ecdh_get_local_pub_key(ChiakiECDH *ecdh, uint8_t *key_out, size_t *key_out_size, const uint8_t *handshake_key, uint8_t *sig_out, size_t *sig_out_size)
{
#ifdef CHIAKI_LIB_ENABLE_MBEDTLS
	mbedtls_md_context_t ctx;
	mbedtls_md_init(&ctx);

#define GOTO_ERROR(err) do { \
	if((err) !=0){ \
		goto error; \
	}} while(0)
	// extract pub key to build dh shared secret
	// this key is sent to the remote server
	GOTO_ERROR(mbedtls_ecp_point_write_binary( &ecdh->ctx.grp, &ecdh->ctx.Q,
		MBEDTLS_ECP_PF_UNCOMPRESSED, key_out_size, key_out, *key_out_size ));

	// https://tls.mbed.org/module-level-design-hashing
	// HMAC
	GOTO_ERROR(mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256) , 1));
	GOTO_ERROR(mbedtls_md_hmac_starts(&ctx, handshake_key, CHIAKI_HANDSHAKE_KEY_SIZE));
	GOTO_ERROR(mbedtls_md_hmac_update(&ctx, key_out, *key_out_size));
	GOTO_ERROR(mbedtls_md_hmac_finish(&ctx, sig_out));
	// SHA256 = 8*32
	*sig_out_size = 32;
#undef GOTO_ERROR
	mbedtls_md_free(&ctx);
	return CHIAKI_ERR_SUCCESS;

error:
	mbedtls_md_free(&ctx);
	return CHIAKI_ERR_UNKNOWN;
#else
	const EC_POINT *point = EC_KEY_get0_public_key(ecdh->key_local);
	if(!point)
		return CHIAKI_ERR_UNKNOWN;

	*key_out_size = EC_POINT_point2oct(ecdh->group, point, POINT_CONVERSION_UNCOMPRESSED, key_out, *key_out_size, NULL);
	if(!(*key_out_size))
		return CHIAKI_ERR_UNKNOWN;

	if(!HMAC(EVP_sha256(), handshake_key, CHIAKI_HANDSHAKE_KEY_SIZE, key_out, *key_out_size, sig_out, (unsigned int *)sig_out_size))
		return CHIAKI_ERR_UNKNOWN;
	return CHIAKI_ERR_SUCCESS;

#endif
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_ecdh_derive_secret(ChiakiECDH *ecdh, uint8_t *secret_out, const uint8_t *remote_key, size_t remote_key_size, const uint8_t *handshake_key, const uint8_t *remote_sig, size_t remote_sig_size)
{
	//compute DH shared key
#ifdef CHIAKI_LIB_ENABLE_MBEDTLS
	// https://github.com/ARMmbed/mbedtls/blob/development/programs/pkey/ecdh_curve25519.c#L151
#define GOTO_ERROR(err) do { \
	if((err) !=0){ \
		goto error;} \
	} while(0)

	GOTO_ERROR(mbedtls_mpi_lset(&ecdh->ctx.Qp.Z, 1));
	// load Qp point form remote PK
	GOTO_ERROR(mbedtls_ecp_point_read_binary(&ecdh->ctx.grp,
		&ecdh->ctx.Qp, remote_key, remote_key_size));

	// build shared secret (diffie-hellman)
	GOTO_ERROR(mbedtls_ecdh_compute_shared(&ecdh->ctx.grp,
		&ecdh->ctx.z, &ecdh->ctx.Qp, &ecdh->ctx.d,
		mbedtls_ctr_drbg_random, &ecdh->drbg));

	// export shared secret to data buffer
	GOTO_ERROR(mbedtls_mpi_write_binary(&ecdh->ctx.z,
		secret_out, CHIAKI_ECDH_SECRET_SIZE));

	return CHIAKI_ERR_SUCCESS;
error:
	return CHIAKI_ERR_UNKNOWN;

#else
	EC_POINT *remote_public_key = EC_POINT_new(ecdh->group);
	if(!remote_public_key)
		return CHIAKI_ERR_UNKNOWN;

	if(!EC_POINT_oct2point(ecdh->group, remote_public_key, remote_key, remote_key_size, NULL))
	{
		EC_POINT_free(remote_public_key);
		return CHIAKI_ERR_UNKNOWN;
	}

	int r = ECDH_compute_key(secret_out, CHIAKI_ECDH_SECRET_SIZE, remote_public_key, ecdh->key_local, NULL);

	EC_POINT_free(remote_public_key);

	if(r != CHIAKI_ECDH_SECRET_SIZE)
		return CHIAKI_ERR_UNKNOWN;

	return CHIAKI_ERR_SUCCESS;
#endif
}
