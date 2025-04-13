/*-
 * The authors of this code are John Ioannidis (ji@tla.org),
 * Angelos D. Keromytis (kermit@csd.uch.gr) and
 * Niels Provos (provos@physnet.uni-hamburg.de).
 *
 * This code was written by John Ioannidis for BSD/OS in Athens, Greece,
 * in November 1995.
 *
 * Ported to OpenBSD and NetBSD, with additional transforms, in December
 * 1996, by Angelos D. Keromytis.
 *
 * Additional transforms and features in 1997 and 1998 by Angelos D.
 * Keromytis and Niels Provos.
 *
 * Additional features in 1999 by Angelos D. Keromytis.
 *
 * Copyright (C) 1995, 1996, 1997, 1998, 1999 by John Ioannidis,
 * Angelos D. Keromytis and Niels Provos.
 *
 * Copyright (C) 2001, Angelos D. Keromytis.
 *
 * Copyright (c) 2024, 2025, Michael Neumann (mneumann@ntecs.de).
 *
 * Permission to use, copy, and modify this software with or without fee
 * is hereby granted, provided that this entire notice is included in
 * all copies of any software which is or includes a copy or
 * modification of this software.
 * You may use this code under the GNU public license if you so wish.
 * Please contribute changes back to the authors under this freer than
 * GPL license so that we may further the use of strong encryption
 * without limitations to all.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, NONE OF THE AUTHORS MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

#ifdef _KERNEL
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>

#include <machine/specialreg.h> /* for CPUID2_AESNI */
#else
#include <sys/param.h>
#include <sys/errno.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

#include <crypto/aesni/aesni.h>
#include <crypto/rijndael/rijndael.h>
#include <crypto/serpent/serpent.h>
#include <crypto/twofish/twofish.h>

#include "cryptoapi.h"

#ifdef _KERNEL
MALLOC_DEFINE(M_CRYPTOAPI, "cryptoapi", "Crypto API");

static int aesni_disable = 0;
// TUNABLE_INT("hw.aesni_disable", &aesni_disable);
SYSCTL_INT(_hw, OID_AUTO, aesni_disable, CTLFLAG_RW, &aesni_disable, 0,
    "Disable AESNI");
#define HAVE_AESNI
#endif

#ifndef _KERNEL
#define __inline
#endif

/**
 * --------------------------------------
 *  Cipher specification
 * --------------------------------------
 */

struct cryptoapi_cipher_spec {
	/**
	 * The name of the cipher, e.g. "aes-xts" that
	 * this specification implements.
	 */
	const char *ciphername;

	/**
	 * Human readable description.
	 */
	const char *description;
	uint16_t blocksize;
	uint16_t ivsize;
	uint16_t ctxsize;
	uint16_t ctxalign;

	/**
	 * Return 0, if:
	 *
	 * - the keysize in bits is supported by the implementation,
	 * - the implementation is supported by the platform
	 *   (e.g. AESNI needs special CPU features),
	 * - and it is enabled by the sysadmin
	 */
	int (*probe)(int keysize_in_bits);

	int (*setkey)(void *ctx, const uint8_t *keydata, int keylen_in_bytes);

	void (*crypt)(const void *ctx, uint8_t *data, int datalen,
	    struct cryptoapi_cipher_iv *iv, bool encrypt);
};

/**
 * --------------------------------------
 *  Utility
 * --------------------------------------
 */

static __inline void
xor_block(uint8_t *dst, const uint8_t *src, int blocksize)
{
	for (int i = 0; i < blocksize; i++)
		dst[i] ^= src[i];
}

static __inline void
xor_block3(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
    int blocksize)
{
	for (int i = 0; i < blocksize; i++)
		dst[i] = src1[i] ^ src2[i];
}

/**
 * Typedef of a block-cipher function that encrypts or decrypts a block
 * of data from `src` to `dst`. Both `src` and `dst` may point to the
 * same data.
 *
 * The length of the block is the block size of the algorithm.
 */
typedef void (*block_fn_t)(const void *ctx, const uint8_t *src, uint8_t *dst);

/**
 *
 * Encrypt blocks of data using CBC (Cipher Block Chaining).
 *
 * Before encrypting a block with the block cipher, the block is XORed
 * with the previous block of data or the IV if it is the first block of
 * data.
 */
static __inline void
encrypt_data_cbc(block_fn_t block_fn, const void *ctx, uint8_t *data,
    int datalen, int blocksize, const uint8_t *iv)
{
	for (int i = 0; i < datalen; i += blocksize) {
		xor_block(data + i, (i == 0) ? iv : (data + i - blocksize),
		    blocksize);

		block_fn(ctx, data + i, data + i);
	}
}

/*
 * Decrypt blocks of data using CBC (Cipher Block Chaining).
 *
 * We start decrypting the blocks in reverse order, from the last block
 * to the first. After decrypting a block with the block cipher, we XOR
 * it.  The last block is XORed with the second last block (which is
 * still encrypted), and so on. Finally, the first block is XORed with
 * the IV after it has been decrypted.
 */
static __inline void
decrypt_data_cbc(block_fn_t block_fn, const void *ctx, uint8_t *data,
    int datalen, int blocksize, const uint8_t *iv)
{
	for (int i = datalen - blocksize; i >= 0; i -= blocksize) {
		block_fn(ctx, data + i, data + i);
		xor_block(data + i, (i == 0) ? iv : (data + i - blocksize),
		    blocksize);
	}
}

/**
 * Encrypts/decrypts a single block using XTS.
 */
static __inline void
crypt_block_xts(const void *ctx, uint8_t *data, uint8_t *iv,
    block_fn_t block_fn, uint8_t *block, int blocklen, int alpha)
{
	int i;
	u_int carry_in, carry_out;

	xor_block3(block, data, iv, blocklen);
	block_fn(ctx, block, data);
	xor_block(data, iv, blocklen);

	/* Exponentiate tweak */
	carry_in = 0;
	for (i = 0; i < blocklen; i++) {
		carry_out = iv[i] & 0x80;
		iv[i] = (iv[i] << 1) | (carry_in ? 1 : 0);
		carry_in = carry_out;
	}
	if (carry_in)
		iv[0] ^= alpha;
}

/**
 * Encrypts/decrypts blocks of data using XTS (without reinit).
 */
static __inline void
crypt_data_xts(const void *ctx, uint8_t *data, int datalen, uint8_t *iv,
    block_fn_t block_fn, uint8_t *block, int blocklen, int alpha)
{
	for (int i = 0; i < datalen; i += blocklen) {
		crypt_block_xts(ctx, data + i, iv, block_fn, block, blocklen,
		    alpha);
	}
	explicit_bzero(block, blocklen);
}

/**
 * --------------------------------------
 * Cipher null
 * --------------------------------------
 */

static int
cipher_null_probe(int keysize_in_bits __unused)
{
	return (0);
}

static int
cipher_null_setkey(void *ctx __unused, const uint8_t *keydata __unused,
    int keylen_in_bytes __unused)
{
	return (0);
}

static void
cipher_null_crypt(const void *ctx __unused, uint8_t *data __unused,
    int datalen __unused, struct cryptoapi_cipher_iv *iv __unused,
    bool encrypt __unused)
{
}

const struct cryptoapi_cipher_spec cipher_null = {
	.ciphername = "null",
	.description = "null - No encryption",
	.blocksize = 1,
	.ivsize = 0,
	.ctxsize = 0,
	.ctxalign = 0,
	.probe = cipher_null_probe,
	.setkey = cipher_null_setkey,
	.crypt = cipher_null_crypt,
};

/**
 * --------------------------------------
 * AES-CBC (Rijndael-128)
 * --------------------------------------
 */

#define AES_BLOCK_LEN 16

static int
aes_cbc_probe(int keysize_in_bits)
{
	if ((keysize_in_bits == 128 || keysize_in_bits == 192 ||
		keysize_in_bits == 256))
		return (0);
	else
		return (-1);
}

static int
aes_cbc_setkey(void *ctx, const uint8_t *keydata, int keylen_in_bytes)
{
	switch (keylen_in_bytes * 8) {
	case 128:
	case 192:
	case 256:
		rijndael_set_key(ctx, keydata, keylen_in_bytes * 8);
		return (0);

	default:
		return (EINVAL);
	}
}

static __inline void
rijndael_encrypt_wrap(const void *ctx, const uint8_t *src, uint8_t *dst)
{
	rijndael_encrypt(ctx, src, dst);
}

static __inline void
rijndael_decrypt_wrap(const void *ctx, const uint8_t *src, uint8_t *dst)
{
	rijndael_decrypt(ctx, src, dst);
}

static void
aes_cbc_crypt(const void *ctx, uint8_t *data, int datalen,
    struct cryptoapi_cipher_iv *iv, bool encrypt)
{
	if (encrypt)
		encrypt_data_cbc(rijndael_encrypt_wrap, ctx, data, datalen,
		    AES_BLOCK_LEN, iv->iv.iv_aes_cbc);
	else
		decrypt_data_cbc(rijndael_decrypt_wrap, ctx, data, datalen,
		    AES_BLOCK_LEN, iv->iv.iv_aes_cbc);
}

const struct cryptoapi_cipher_spec cipher_aes_cbc = {
	.ciphername = "aes-cbc",
	.description = "AES-CBC (Rijndael-128) in software",
	.blocksize = AES_BLOCK_LEN,
	.ivsize = AES_BLOCK_LEN,
	.ctxsize = sizeof(rijndael_ctx),
	/* there are no alignment requirements imposed by the algorithm,
		but using 16 can't do any harm either */
	.ctxalign = 16,
	.probe = aes_cbc_probe,
	.setkey = aes_cbc_setkey,
	.crypt = aes_cbc_crypt,
};

/**
 * --------------------------------------
 * AES-XTS
 * --------------------------------------
 */

#define AES_XTS_BLOCK_LEN 16
#define AES_XTS_IV_LEN	  8
#define AES_XTS_ALPHA	  0x87 /* GF(2^128) generator polynomial */

struct aes_xts_ctx {
	rijndael_ctx key1;
	rijndael_ctx key2;
};

static bool
aes_xts_valid_keysize_in_bits(int keysize_in_bits)
{
	switch (keysize_in_bits) {
	case 256:
	case 512:
		return true;
	default:
		return false;
	}
}

static int
aes_xts_probe(int keysize_in_bits)
{
	if (aes_xts_valid_keysize_in_bits(keysize_in_bits))
		return (0);

	return (-1);
}

static int
aes_xts_setkey(void *_ctx, const uint8_t *keydata, int keylen_in_bytes)
{
	struct aes_xts_ctx *ctx = _ctx;

	if (!aes_xts_valid_keysize_in_bits(keylen_in_bytes * 8))
		return (EINVAL);

	rijndael_set_key(&ctx->key1, keydata, (keylen_in_bytes / 2) * 8);
	rijndael_set_key(&ctx->key2, keydata + (keylen_in_bytes / 2),
	    (keylen_in_bytes / 2) * 8);

	return (0);
}

static __inline void
aes_xts_reinit(const struct aes_xts_ctx *ctx, u_int8_t *iv)
{
#if 0
	/*
	 * Prepare tweak as E_k2(IV). IV is specified as LE representation
	 * of a 64-bit block number which we allow to be passed in directly.
	 */
	/* XXX: possibly use htole64? */
#endif
	/* Last 64 bits of IV are always zero */
	bzero(iv + AES_XTS_IV_LEN, AES_XTS_IV_LEN);

	rijndael_encrypt(&ctx->key2, iv, iv);
}

static void
aes_xts_crypt(const void *_ctx, uint8_t *data, int datalen,
    struct cryptoapi_cipher_iv *_iv, bool encrypt)
{
	uint8_t block[AES_XTS_BLOCK_LEN];
	uint8_t *iv = _iv->iv.iv_aes_xts;
	const struct aes_xts_ctx *ctx = _ctx;

	aes_xts_reinit(ctx, iv);

	crypt_data_xts(&ctx->key1, data, datalen, iv,
	    encrypt ? rijndael_encrypt_wrap : rijndael_decrypt_wrap, block,
	    AES_XTS_BLOCK_LEN, AES_XTS_ALPHA);
}

const struct cryptoapi_cipher_spec cipher_aes_xts = {
	.ciphername = "aes-xts",
	.description = "AES-XTS (in software)",
	.blocksize = AES_XTS_BLOCK_LEN,
	.ivsize = 16,
	.ctxsize = sizeof(struct aes_xts_ctx),
	/* there are no alignment requirements imposed by the algorithm,
		but using 16 can't do any harm either */
	.ctxalign = 16,
	.probe = aes_xts_probe,
	.setkey = aes_xts_setkey,
	.crypt = aes_xts_crypt,
};

/**
 * --------------------------------------
 * AES-CBC in hardware (AES-NI)
 * --------------------------------------
 */

#ifdef HAVE_AESNI

struct aesni_ctx {
	uint8_t enc_schedule[AES_SCHED_LEN] __aligned(AESNI_ALIGN);
	uint8_t dec_schedule[AES_SCHED_LEN] __aligned(AESNI_ALIGN);
	uint8_t xts_schedule[AES_SCHED_LEN] __aligned(AESNI_ALIGN);
	int rounds;
};

#define ASSERT_AESNI_ALIGNED(ptr)                                   \
	KKASSERT((((uintptr_t)ptr) % AESNI_ALIGN) == 0);            \
	if (__predict_false((((uintptr_t)ptr) % AESNI_ALIGN) != 0)) \
		panic("AESNI misaligned");

static int
cipher_aesni_cbc_probe(int keysize_in_bits)
{
	if (aesni_disable)
		return (-1);

	if ((cpu_feature2 & CPUID2_AESNI) == 0)
		return (EINVAL);

	if ((keysize_in_bits == 128 || keysize_in_bits == 192 ||
		keysize_in_bits == 256))
		return (0);

	return (-1);
}

static int
cipher_aesni_cbc_setkey(void *_ctx, const uint8_t *keydata, int keylen_in_bytes)
{
	struct aesni_ctx *ctx = _ctx;
	int rounds;

	ASSERT_AESNI_ALIGNED(&ctx->enc_schedule);
	ASSERT_AESNI_ALIGNED(&ctx->dec_schedule);

	bzero(ctx, sizeof(*ctx));

	switch (keylen_in_bytes * 8) {
	case 128:
		rounds = AES128_ROUNDS;
		break;
	case 192:
		rounds = AES192_ROUNDS;
		break;
	case 256:
		rounds = AES256_ROUNDS;
		break;
	default:
		return (EINVAL);
	}

	ctx->rounds = rounds;

	aesni_set_enckey(keydata, ctx->enc_schedule, rounds);
	aesni_set_deckey(ctx->enc_schedule, ctx->dec_schedule, rounds);

	return (0);
}

static void
cipher_aesni_cbc_crypt(const void *_ctx, uint8_t *data, int datalen,
    struct cryptoapi_cipher_iv *iv, bool encrypt)
{
	const struct aesni_ctx *ctx = _ctx;

	if (encrypt)
		aesni_encrypt_cbc(ctx->rounds, ctx->enc_schedule, datalen, data,
		    data, iv->iv.iv_aesni);
	else
		aesni_decrypt_cbc(ctx->rounds, ctx->dec_schedule, datalen, data,
		    iv->iv.iv_aesni);
}

const struct cryptoapi_cipher_spec cipher_aesni_cbc = {
	.ciphername = "aes-cbc",
	.description = "AES-CBC w/ CPU AESNI instruction",
	.blocksize = AES_BLOCK_LEN,
	.ivsize = AES_BLOCK_LEN,
	.ctxsize = sizeof(struct aesni_ctx),
	.ctxalign = AESNI_ALIGN,
	.probe = cipher_aesni_cbc_probe,
	.setkey = cipher_aesni_cbc_setkey,
	.crypt = cipher_aesni_cbc_crypt,
};

#endif

/**
 * --------------------------------------
 * AES-XTS in hardware (AES-NI)
 * --------------------------------------
 */

#ifdef HAVE_AESNI

static int
cipher_aesni_xts_probe(int keysize_in_bits)
{
	if (aesni_disable)
		return (-1);

	if ((cpu_feature2 & CPUID2_AESNI) == 0)
		return (EINVAL);

	if ((keysize_in_bits == 256 || keysize_in_bits == 512))
		return (0);

	return (-1);
}

static int
cipher_aesni_xts_setkey(void *_ctx, const uint8_t *keydata, int keylen_in_bytes)
{
	struct aesni_ctx *ctx = _ctx;
	int rounds;

	ASSERT_AESNI_ALIGNED(&ctx->enc_schedule);
	ASSERT_AESNI_ALIGNED(&ctx->dec_schedule);
	ASSERT_AESNI_ALIGNED(&ctx->xts_schedule);

	bzero(ctx, sizeof(*ctx));

	switch (keylen_in_bytes * 8) {
	case 256:
		rounds = AES128_ROUNDS;
		break;
	case 512:
		rounds = AES256_ROUNDS;
		break;
	default:
		return (EINVAL);
	}

	ctx->rounds = rounds;

	aesni_set_enckey(keydata, ctx->enc_schedule, rounds);
	aesni_set_deckey(ctx->enc_schedule, ctx->dec_schedule, rounds);
	aesni_set_enckey(keydata + (keylen_in_bytes / 2), ctx->xts_schedule,
	    rounds);

	return (0);
}

static void
cipher_aesni_xts_crypt(const void *_ctx, uint8_t *data, int datalen,
    struct cryptoapi_cipher_iv *iv, bool encrypt)
{
	const struct aesni_ctx *ctx = _ctx;

	if (encrypt)
		aesni_encrypt_xts(ctx->rounds, ctx->enc_schedule,
		    ctx->xts_schedule, datalen, data, data, iv->iv.iv_aesni);
	else
		aesni_decrypt_xts(ctx->rounds, ctx->dec_schedule,
		    ctx->xts_schedule, datalen, data, data, iv->iv.iv_aesni);
}

const struct cryptoapi_cipher_spec cipher_aesni_xts = {
	.ciphername = "aes-xts",
	.description = "AES-XTS w/ CPU AESNI instruction",
	.blocksize = AES_BLOCK_LEN,
	.ivsize = AES_BLOCK_LEN,
	.ctxsize = sizeof(struct aesni_ctx),
	.ctxalign = AESNI_ALIGN,
	.probe = cipher_aesni_xts_probe,
	.setkey = cipher_aesni_xts_setkey,
	.crypt = cipher_aesni_xts_crypt,
};

#endif

/**
 * --------------------------------------
 * TWOFISH-CBC
 * --------------------------------------
 */

#define TWOFISH_BLOCK_LEN 16

static int
twofish_cbc_probe(int keysize_in_bits)
{
	if ((keysize_in_bits == 128 || keysize_in_bits == 192 ||
		keysize_in_bits == 256))
		return (0);
	else
		return (-1);
}

static int
twofish_cbc_setkey(void *ctx, const uint8_t *keydata, int keylen_in_bytes)
{
	switch (keylen_in_bytes * 8) {
	case 128:
	case 192:
	case 256:
		twofish_set_key(ctx, keydata, keylen_in_bytes * 8);
		return (0);

	default:
		return (EINVAL);
	}
}

static __inline void
twofish_encrypt_wrap(const void *ctx, const uint8_t *src, uint8_t *dst)
{
	twofish_encrypt((const twofish_ctx *)ctx, src, dst);
}

static __inline void
twofish_decrypt_wrap(const void *ctx, const uint8_t *src, uint8_t *dst)
{
	twofish_decrypt((const twofish_ctx *)ctx, src, dst);
}

static void
twofish_cbc_crypt(const void *ctx, uint8_t *data, int datalen,
    struct cryptoapi_cipher_iv *iv, bool encrypt)
{
	if (encrypt)
		encrypt_data_cbc(twofish_encrypt_wrap, ctx, data, datalen,
		    TWOFISH_BLOCK_LEN, iv->iv.iv_twofish_cbc);
	else
		decrypt_data_cbc(twofish_decrypt_wrap, ctx, data, datalen,
		    TWOFISH_BLOCK_LEN, iv->iv.iv_twofish_cbc);
}

const struct cryptoapi_cipher_spec cipher_twofish_cbc = {
	.ciphername = "twofish-cbc",
	.description = "Twofish-CBC",
	.blocksize = TWOFISH_BLOCK_LEN,
	.ivsize = TWOFISH_BLOCK_LEN,
	.ctxsize = sizeof(twofish_ctx),
	/* there are no alignment requirements imposed by the algorithm,
		but using 16 can't do any harm either */
	.ctxalign = 16,
	.probe = twofish_cbc_probe,
	.setkey = twofish_cbc_setkey,
	.crypt = twofish_cbc_crypt,
};

/**
 * --------------------------------------
 * TWOFISH-XTS
 * --------------------------------------
 */

#define TWOFISH_XTS_BLOCK_LEN 16
#define TWOFISH_XTS_IV_LEN    8

struct twofish_xts_ctx {
	twofish_ctx key1;
	twofish_ctx key2;
};

static __inline void
twofish_xts_reinit(const struct twofish_xts_ctx *ctx, uint8_t *iv)
{
#if 0
	u_int64_t blocknum;
#endif

#if 0
	/*
	 * Prepare tweak as E_k2(IV). IV is specified as LE representation
	 * of a 64-bit block number which we allow to be passed in directly.
	 */
	/* XXX: possibly use htole64? */
#endif
	/* Last 64 bits of IV are always zero */
	bzero(iv + TWOFISH_XTS_IV_LEN, TWOFISH_XTS_IV_LEN);

	twofish_encrypt(&ctx->key2, iv, iv);
}

static int
twofish_xts_probe(int keysize_in_bits)
{
	if ((keysize_in_bits == 256 || keysize_in_bits == 512))
		return (0);
	else
		return (-1);
}

static int
twofish_xts_setkey(void *_ctx, const uint8_t *keydata, int keylen_in_bytes)
{
	struct twofish_xts_ctx *ctx = _ctx;

	switch (keylen_in_bytes * 8) {
	case 256:
	case 512:
		twofish_set_key(&ctx->key1, keydata, (keylen_in_bytes / 2) * 8);
		twofish_set_key(&ctx->key2, keydata + (keylen_in_bytes / 2),
		    (keylen_in_bytes / 2) * 8);
		return (0);

	default:
		return (EINVAL);
	}
}

static void
twofish_xts_crypt(const void *_ctx, uint8_t *data, int datalen,
    struct cryptoapi_cipher_iv *_iv, bool encrypt)
{
	uint8_t block[TWOFISH_XTS_BLOCK_LEN];
	uint8_t *iv = _iv->iv.iv_twofish_xts;
	const struct twofish_xts_ctx *ctx = _ctx;

	twofish_xts_reinit(ctx, iv);

	crypt_data_xts(&ctx->key1, data, datalen, iv,
	    encrypt ? twofish_encrypt_wrap : twofish_decrypt_wrap, block,
	    TWOFISH_XTS_BLOCK_LEN, AES_XTS_ALPHA);
}

const struct cryptoapi_cipher_spec cipher_twofish_xts = {
	.ciphername = "twofish-xts",
	.description = "Twofish-XTS",
	.blocksize = TWOFISH_XTS_BLOCK_LEN,
	.ivsize = TWOFISH_XTS_IV_LEN,
	.ctxsize = sizeof(struct twofish_xts_ctx),
	/* there are no alignment requirements imposed by the algorithm,
		but using 16 can't do any harm either */
	.ctxalign = 16,
	.probe = twofish_xts_probe,
	.setkey = twofish_xts_setkey,
	.crypt = twofish_xts_crypt,
};

/**
 * --------------------------------------
 * Serpent-CBC
 * --------------------------------------
 */

#define SERPENT_BLOCK_LEN 16

static int
serpent_cbc_probe(int keysize_in_bits)
{
	if ((keysize_in_bits == 128 || keysize_in_bits == 192 ||
		keysize_in_bits == 256))
		return (0);
	else
		return (-1);
}

static int
serpent_cbc_setkey(void *ctx, const uint8_t *keydata, int keylen_in_bytes)
{
	switch (keylen_in_bytes * 8) {
	case 128:
	case 192:
	case 256:
		serpent_set_key(ctx, keydata, keylen_in_bytes * 8);
		return (0);

	default:
		return (EINVAL);
	}
}

static __inline void
serpent_encrypt_wrap(const void *ctx, const uint8_t *src, uint8_t *dst)
{
	serpent_encrypt((const serpent_ctx *)ctx, src, dst);
}

static __inline void
serpent_decrypt_wrap(const void *ctx, const uint8_t *src, uint8_t *dst)
{
	serpent_decrypt((const serpent_ctx *)ctx, src, dst);
}

static void
serpent_cbc_crypt(const void *ctx, uint8_t *data, int datalen,
    struct cryptoapi_cipher_iv *iv, bool encrypt)
{
	if (encrypt)
		encrypt_data_cbc(serpent_encrypt_wrap, ctx, data, datalen,
		    SERPENT_BLOCK_LEN, iv->iv.iv_serpent_cbc);
	else
		decrypt_data_cbc(serpent_decrypt_wrap, ctx, data, datalen,
		    SERPENT_BLOCK_LEN, iv->iv.iv_serpent_cbc);
}

const struct cryptoapi_cipher_spec cipher_serpent_cbc = {
	.ciphername = "serpent-cbc",
	.description = "Serpent-CBC",
	.blocksize = SERPENT_BLOCK_LEN,
	.ivsize = SERPENT_BLOCK_LEN,
	.ctxsize = sizeof(serpent_ctx),
	/* there are no alignment requirements imposed by the algorithm,
		but using 16 can't do any harm either */
	.ctxalign = 16,
	.probe = serpent_cbc_probe,
	.setkey = serpent_cbc_setkey,
	.crypt = serpent_cbc_crypt,
};

/**
 * --------------------------------------
 * Serpent-XTS
 * --------------------------------------
 */

#define SERPENT_XTS_BLOCK_LEN 16
#define SERPENT_XTS_IV_LEN    8

struct serpent_xts_ctx {
	serpent_ctx key1;
	serpent_ctx key2;
};

static __inline void
serpent_xts_reinit(const struct serpent_xts_ctx *ctx, uint8_t *iv)
{
#if 0
	u_int64_t blocknum;
	u_int i;
#endif

#if 0
	/*
	 * Prepare tweak as E_k2(IV). IV is specified as LE representation
	 * of a 64-bit block number which we allow to be passed in directly.
	 */
	/* XXX: possibly use htole64? */
#endif
	/* Last 64 bits of IV are always zero */
	bzero(iv + SERPENT_XTS_IV_LEN, SERPENT_XTS_IV_LEN);

	serpent_encrypt(&ctx->key2, iv, iv);
}

static int
serpent_xts_probe(int keysize_in_bits)
{
	if ((keysize_in_bits == 256 || keysize_in_bits == 512))
		return (0);
	else
		return (-1);
}

static int
serpent_xts_setkey(void *_ctx, const uint8_t *keydata, int keylen_in_bytes)
{
	struct serpent_xts_ctx *ctx = _ctx;

	switch (keylen_in_bytes * 8) {
	case 256:
	case 512:
		serpent_set_key(&ctx->key1, keydata, (keylen_in_bytes / 2) * 8);
		serpent_set_key(&ctx->key2, keydata + (keylen_in_bytes / 2),
		    (keylen_in_bytes / 2) * 8);
		return (0);

	default:
		return (EINVAL);
	}
}

static void
serpent_xts_crypt(const void *_ctx, uint8_t *data, int datalen,
    struct cryptoapi_cipher_iv *_iv, bool encrypt)
{
	uint8_t block[SERPENT_XTS_BLOCK_LEN];
	uint8_t *iv = _iv->iv.iv_serpent_xts;
	const struct serpent_xts_ctx *ctx = _ctx;

	serpent_xts_reinit(ctx, iv);

	crypt_data_xts(&ctx->key1, data, datalen, iv,
	    encrypt ? serpent_encrypt_wrap : serpent_decrypt_wrap, block,
	    SERPENT_XTS_BLOCK_LEN, AES_XTS_ALPHA);
}

const struct cryptoapi_cipher_spec cipher_serpent_xts = {
	.ciphername = "serpent-xts",
	.description = "Serpent-XTS",
	.blocksize = SERPENT_XTS_BLOCK_LEN,
	.ivsize = SERPENT_XTS_IV_LEN,
	.ctxsize = sizeof(struct serpent_xts_ctx),
	/* there are no alignment requirements imposed by the algorithm,
		but using 16 can't do any harm either */
	.ctxalign = 16,
	.probe = serpent_xts_probe,
	.setkey = serpent_xts_setkey,
	.crypt = serpent_xts_crypt,
};

/**
 * Cipher registration
 */

static cryptoapi_cipher_t cryptoapi_ciphers[] = {
	&cipher_null,

#ifdef HAVE_AESNI
	/* first probe AESNI, then fallback to software AES */
	&cipher_aesni_cbc,
	&cipher_aesni_xts,
#endif

	/* AES in software */
	&cipher_aes_cbc,
	&cipher_aes_xts,

	&cipher_twofish_cbc,
	&cipher_twofish_xts,
	&cipher_serpent_cbc,
	&cipher_serpent_xts,
};

/**
 * --------------------------------------
 *  API
 * --------------------------------------
 */

cryptoapi_cipher_t
cryptoapi_cipher_find(const char *ciphername, int keysize_in_bits)
{
	cryptoapi_cipher_t cipher;
	size_t i;

	for (i = 0; i < nitems(cryptoapi_ciphers); i++) {
		cipher = cryptoapi_ciphers[i];
		if ((strcasecmp(cipher->ciphername, ciphername) == 0) &&
		    (cipher->probe(keysize_in_bits) == 0)) {
			return cipher;
		}
	}

	return NULL;
}

const char *
cryptoapi_cipher_get_description(cryptoapi_cipher_t cipher)
{
	if (cipher == NULL)
		return NULL;

	return cipher->description;
}

#ifdef _KERNEL
static __inline bool
is_ptr_aligned(void *ptr, int alignment)
{
	return (((uintptr_t)ptr % alignment) == 0);
}

static __inline void *
kmalloc_aligned(int size, int alignment, void **origptr)
{
	void *ptr;

	ptr = kmalloc(size, M_CRYPTOAPI, M_WAITOK);

	if (is_ptr_aligned(ptr, alignment)) {
		*origptr = ptr;
		return ptr;
	}

	kfree(ptr, M_CRYPTOAPI);

	ptr = kmalloc(size + alignment, M_CRYPTOAPI, M_WAITOK);

	uintptr_t offset = alignment - ((uintptr_t)ptr % alignment);

	KKASSERT(offset < alignment);

	*origptr = ptr;

	ptr = (uint8_t *)ptr + offset;

	KKASSERT(is_ptr_aligned(ptr, alignment));

	return (ptr);
}
#endif

int
cryptoapi_cipher_initsession(cryptoapi_cipher_t cipher,
    cryptoapi_cipher_session_t *session)
{
	if (session == NULL)
		return (EINVAL);

	if (__predict_false(
		cipher->ivsize > sizeof(struct cryptoapi_cipher_iv))) {
#ifdef _KERNEL
		kprintf("FATAL: struct cryptoapi_cipher_iv has wrong size\n");
#else
		printf("FATAL: struct cryptoapi_cipher_iv has wrong size\n");
#endif
		return (EINVAL);
	}

	bzero(session, sizeof(cryptoapi_cipher_session_t));

	if (cipher->ctxsize > 0) {
#ifdef _KERNEL
		session->context = kmalloc_aligned(cipher->ctxsize,
		    cipher->ctxalign, &session->origptr);
#else
		session->context = session->origptr =
		    aligned_alloc(cipher->ctxsize, cipher->ctxalign);
#endif

		if (session->context == NULL)
			return (ENOMEM);

			/**
			 * Fill context with random data, just in case
			 * someone forgets to initialize it.
			 */
#ifdef _KERNEL
		karc4random_buf(session->context, cipher->ctxsize);
#else
		arc4random_buf(session->context, cipher->ctxsize);
#endif

	} else {
		session->context = NULL;
		session->origptr = NULL;
	}

	session->cipher = cipher;

	return (0);
}

int
cryptoapi_cipher_freesession(cryptoapi_cipher_session_t *session)
{
	if (session == NULL)
		return (EINVAL);

	if (session->cipher == NULL)
		return (EINVAL);

	if (session->context) {
		memset(session->context, 0xFF, session->cipher->ctxsize);
		explicit_bzero(session->context, session->cipher->ctxsize);
	}

	if (session->origptr) {
#ifdef _KERNEL
		kfree(session->origptr, M_CRYPTOAPI);
#else
		free(session->origptr);
#endif
	}

	bzero(session, sizeof(*session));

	return (0);
}

int
cryptoapi_cipher_setkey(cryptoapi_cipher_session_t *session,
    const uint8_t *keydata, int keylen_in_bytes)
{
	return session->cipher->setkey(session->context, keydata,
	    keylen_in_bytes);
}

int
cryptoapi_cipher_encrypt(const cryptoapi_cipher_session_t *session,
    uint8_t *data, int datalen, struct cryptoapi_cipher_iv *iv)
{
	if ((datalen % session->cipher->blocksize) != 0)
		return (EINVAL);

	session->cipher->crypt(session->context, data, datalen, iv, true);

	return (0);
}

int
cryptoapi_cipher_decrypt(const cryptoapi_cipher_session_t *session,
    uint8_t *data, int datalen, struct cryptoapi_cipher_iv *iv)
{
	if ((datalen % session->cipher->blocksize) != 0)
		return (EINVAL);

	session->cipher->crypt(session->context, data, datalen, iv, false);

	return (0);
}

int
cryptoapi_cipher_crypt(const cryptoapi_cipher_session_t *session, uint8_t *data,
    int datalen, struct cryptoapi_cipher_iv *iv, cryptoapi_cipher_mode mode)
{
	switch (mode) {
	case CRYPTOAPI_CIPHER_ENCRYPT:
		return cryptoapi_cipher_encrypt(session, data, datalen, iv);
	case CRYPTOAPI_CIPHER_DECRYPT:
		return cryptoapi_cipher_decrypt(session, data, datalen, iv);
	default:
		return EINVAL;
	}
}
