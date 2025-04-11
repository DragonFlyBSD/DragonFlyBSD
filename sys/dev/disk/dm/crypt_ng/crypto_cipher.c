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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

#include <machine/specialreg.h> /* for CPUID2_AESNI */

#include <dev/disk/dm/crypt_ng/crypto_cipher.h>

#include <crypto/aesni/aesni.h>

static int aesni_disable = 0;
// TUNABLE_INT("hw.aesni_disable", &aesni_disable);
SYSCTL_INT(_hw, OID_AUTO, aesni_disable, CTLFLAG_RW, &aesni_disable, 0,
    "Disable AESNI");

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
static __inline int
encrypt_data_cbc(block_fn_t block_fn, const void *ctx, uint8_t *data,
    int datalen, int blocksize, const uint8_t *iv)
{
	if ((datalen % blocksize) != 0)
		return EINVAL;

	for (int i = 0; i < datalen; i += blocksize) {
		xor_block(data + i, (i == 0) ? iv : (data + i - blocksize),
		    blocksize);

		block_fn(ctx, data + i, data + i);
	}

	return (0);
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
static __inline int
decrypt_data_cbc(block_fn_t block_fn, const void *ctx, uint8_t *data,
    int datalen, int blocksize, const uint8_t *iv)
{
	if ((datalen % blocksize) != 0)
		return EINVAL;

	for (int i = datalen - blocksize; i >= 0; i -= blocksize) {
		block_fn(ctx, data + i, data + i);
		xor_block(data + i, (i == 0) ? iv : (data + i - blocksize),
		    blocksize);
	}

	return (0);
}

/**
 * --------------------------------------
 * Cipher null
 * --------------------------------------
 */

static int
cipher_null_probe(const char *algo_name, const char *mode_name __unused,
    int keysize_in_bits __unused)
{
	if (strcmp(algo_name, "null") == 0)
		return (0);
	return (-1);
}

static int
cipher_null_setkey(struct crypto_cipher_context *ctx __unused,
    const uint8_t *keydata __unused, int keylen_in_bytes __unused)
{
	return (0);
}

static int
cipher_null_encrypt(const struct crypto_cipher_context *ctx __unused,
    uint8_t *data __unused, int datalen __unused,
    struct crypto_cipher_iv *iv __unused)
{
	return (0);
}

static int
cipher_null_decrypt(const struct crypto_cipher_context *ctx __unused,
    uint8_t *data, int datalen, struct crypto_cipher_iv *iv __unused)
{
	return (0);
}

const struct crypto_cipher cipher_null = {
	"null",
	"null",
	4,
	0,
	0,
	cipher_null_probe,
	cipher_null_setkey,
	cipher_null_encrypt,
	cipher_null_decrypt,
};

/**
 * --------------------------------------
 * AES-CBC (Rijndael-128)
 * --------------------------------------
 */

#define AES_BLOCK_LEN 16

static int
aes_cbc_probe(const char *algo_name, const char *mode_name, int keysize_in_bits)
{
	if ((strcmp(algo_name, "aes") == 0) &&
	    (strcmp(mode_name, "cbc") == 0) &&
	    (keysize_in_bits == 128 || keysize_in_bits == 192 ||
		keysize_in_bits == 256))
		return (0);
	else
		return (-1);
}

static int
aes_cbc_setkey(struct crypto_cipher_context *ctx, const uint8_t *keydata,
    int keylen_in_bytes)
{
	switch (keylen_in_bytes * 8) {
	case 128:
	case 192:
	case 256:
		rijndael_set_key((void *)ctx, keydata, keylen_in_bytes * 8);
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

static int
aes_cbc_encrypt(const struct crypto_cipher_context *ctx, uint8_t *data,
    int datalen, struct crypto_cipher_iv *iv)
{
	return encrypt_data_cbc(rijndael_encrypt_wrap, (const void *)ctx, data,
	    datalen, AES_BLOCK_LEN, (uint8_t *)iv);
}

static int
aes_cbc_decrypt(const struct crypto_cipher_context *ctx, uint8_t *data,
    int datalen, struct crypto_cipher_iv *iv)
{
	return decrypt_data_cbc(rijndael_decrypt_wrap, (const void *)ctx, data,
	    datalen, AES_BLOCK_LEN, (uint8_t *)iv);
}

const struct crypto_cipher cipher_aes_cbc = {
	"aes-cbc",
	"AES-CBC (Rijndael-128) in software",
	AES_BLOCK_LEN,
	AES_BLOCK_LEN,
	sizeof(rijndael_ctx),
	aes_cbc_probe,
	aes_cbc_setkey,
	aes_cbc_encrypt,
	aes_cbc_decrypt,
};

/**
 * --------------------------------------
 * AES-XTS
 * --------------------------------------
 */

#define AES_XTS_BLOCK_LEN 16
#define AES_XTS_IV_LEN	  8
#define AES_XTS_ALPHA	  0x87 /* GF(2^128) generator polynomial */

static void
aes_xts_crypt_block(const void *ctx, uint8_t *data, uint8_t *iv,
    block_fn_t block_fn, uint8_t block[AES_XTS_BLOCK_LEN])
{
	u_int i, carry_in, carry_out;

	xor_block3(block, data, iv, AES_XTS_BLOCK_LEN);
	block_fn(ctx, block, data);
	xor_block(data, iv, AES_XTS_BLOCK_LEN);

	/* Exponentiate tweak */
	carry_in = 0;
	for (i = 0; i < AES_XTS_BLOCK_LEN; i++) {
		carry_out = iv[i] & 0x80;
		iv[i] = (iv[i] << 1) | (carry_in ? 1 : 0);
		carry_in = carry_out;
	}
	if (carry_in)
		iv[0] ^= AES_XTS_ALPHA;
}

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
aes_xts_probe(const char *algo_name, const char *mode_name, int keysize_in_bits)
{
	if (strcmp(algo_name, "aes") != 0)
		return (-1);

	if (strcmp(mode_name, "xts") != 0)
		return (-1);

	if (aes_xts_valid_keysize_in_bits(keysize_in_bits))
		return (0);

	return (-1);
}

static int
aes_xts_setkey(struct crypto_cipher_context *ctx, const uint8_t *keydata,
    int keylen_in_bytes)
{
	if (!aes_xts_valid_keysize_in_bits(keylen_in_bytes * 8))
		return (EINVAL);

	rijndael_set_key(&ctx->_ctx._aes_xts.key1, keydata,
	    (keylen_in_bytes / 2) * 8);
	rijndael_set_key(&ctx->_ctx._aes_xts.key2,
	    keydata + (keylen_in_bytes / 2), (keylen_in_bytes / 2) * 8);

	return (0);
}

static void
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

static int
aes_xts_encrypt(const struct crypto_cipher_context *_ctx, uint8_t *data,
    int datalen, struct crypto_cipher_iv *_iv)
{
	uint8_t block[AES_XTS_BLOCK_LEN];
	uint8_t *iv = _iv->_iv._aes_xts;
	const struct aes_xts_ctx *ctx = &_ctx->_ctx._aes_xts;

	if ((datalen % AES_XTS_BLOCK_LEN) != 0)
		return EINVAL;

	aes_xts_reinit(ctx, iv);
	for (int i = 0; i < datalen; i += AES_XTS_BLOCK_LEN) {
		aes_xts_crypt_block(&ctx->key1, data + i, iv,
		    rijndael_encrypt_wrap, block);
	}
	explicit_bzero(block, sizeof(block));

	return (0);
}

static int
aes_xts_decrypt(const struct crypto_cipher_context *_ctx, uint8_t *data,
    int datalen, struct crypto_cipher_iv *_iv)
{
	uint8_t block[AES_XTS_BLOCK_LEN];
	uint8_t *iv = _iv->_iv._aes_xts;
	const struct aes_xts_ctx *ctx = &_ctx->_ctx._aes_xts;

	if ((datalen % AES_XTS_BLOCK_LEN) != 0)
		return EINVAL;

	aes_xts_reinit(ctx, iv);
	for (int i = 0; i < datalen; i += AES_XTS_BLOCK_LEN) {
		aes_xts_crypt_block(ctx, data + i, iv, rijndael_decrypt_wrap,
		    block);
	}

	explicit_bzero(block, sizeof(block));

	return (0);
}

const struct crypto_cipher cipher_aes_xts = {
	"aes-xts",
	"AES-XTS (in software)",
	AES_XTS_BLOCK_LEN,
	16,
	sizeof(struct aes_xts_ctx),
	aes_xts_probe,
	aes_xts_setkey,
	aes_xts_encrypt,
	aes_xts_decrypt,
};

/**
 * --------------------------------------
 * AES-CBC in hardware (AES-NI)
 * --------------------------------------
 */

#define AESNI_CTX(ctx) (ctx->_ctx._aesni)
#define AESNI_IV(iv)   (iv->_iv._aesni.iv)

// TODO: how to improve alignment?
#define AESNI_ALIGNED_KEY_SCHEDULES(ctx, CONST)                     \
	((CONST struct aesni_key_schedules                          \
		*)((((uintptr_t)((CONST uint8_t *)&(                \
			AESNI_CTX(ctx).key_schedules.schedules))) + \
		       (AESNI_ALIGN - 1)) &                         \
	    (~(AESNI_ALIGN - 1))))

#define AESNI_ALIGNED_ENC_SCHEDULE(ctx, CONST) \
	(AESNI_ALIGNED_KEY_SCHEDULES(ctx, CONST)->enc_schedule)

#define AESNI_ALIGNED_DEC_SCHEDULE(ctx, CONST) \
	(AESNI_ALIGNED_KEY_SCHEDULES(ctx, CONST)->dec_schedule)

#define AESNI_ALIGNED_XTS_SCHEDULE(ctx, CONST) \
	(AESNI_ALIGNED_KEY_SCHEDULES(ctx, CONST)->xts_schedule)

#define KKASSERT_AESNI_ALIGNED(ptr) \
	KKASSERT((((uintptr_t)(const uint8_t *)ptr) % AESNI_ALIGN) == 0)

static int
cipher_aesni_cbc_probe(const char *algo_name, const char *mode_name,
    int keysize_in_bits)
{
	if (aesni_disable)
		return (-1);

	if ((cpu_feature2 & CPUID2_AESNI) == 0)
		return (EINVAL);

	if ((strcmp(algo_name, "aes") == 0) &&
	    (strcmp(mode_name, "cbc") == 0) &&
	    (keysize_in_bits == 128 || keysize_in_bits == 192 ||
		keysize_in_bits == 256))
		return (0);

	return (-1);
}

static int
cipher_aesni_cbc_setkey(struct crypto_cipher_context *ctx,
    const uint8_t *keydata, int keylen_in_bytes)
{
	bzero(ctx, sizeof(*ctx));
	int rounds;

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

	uint8_t *enc_schedule = AESNI_ALIGNED_ENC_SCHEDULE(ctx, );
	uint8_t *dec_schedule = AESNI_ALIGNED_DEC_SCHEDULE(ctx, );

	AESNI_CTX(ctx).rounds = rounds;

	aesni_set_enckey(keydata, enc_schedule, rounds);
	aesni_set_deckey(enc_schedule, dec_schedule, rounds);

	return (0);
}

static int
cipher_aesni_cbc_encrypt(const struct crypto_cipher_context *ctx, uint8_t *data,
    int datalen, struct crypto_cipher_iv *iv)
{
	if ((datalen % AES_BLOCK_LEN) != 0)
		return (EINVAL);

	const uint8_t *enc_schedule = AESNI_ALIGNED_ENC_SCHEDULE(ctx, const);

	KKASSERT_AESNI_ALIGNED(enc_schedule);

	aesni_encrypt_cbc(AESNI_CTX(ctx).rounds, enc_schedule, datalen, data,
	    data, AESNI_IV(iv));

	return (0);
}

static int
cipher_aesni_cbc_decrypt(const struct crypto_cipher_context *ctx, uint8_t *data,
    int datalen, struct crypto_cipher_iv *iv)
{
	if ((datalen % AES_BLOCK_LEN) != 0)
		return (EINVAL);

	const uint8_t *dec_schedule = AESNI_ALIGNED_DEC_SCHEDULE(ctx, const);

	KKASSERT_AESNI_ALIGNED(dec_schedule);

	aesni_decrypt_cbc(AESNI_CTX(ctx).rounds, dec_schedule, datalen, data,
	    AESNI_IV(iv));

	return (0);
}

const struct crypto_cipher cipher_aesni_cbc = {
	"aesni-cbc",
	"AES-CBC w/ CPU AESNI instruction",
	AES_BLOCK_LEN,
	AES_BLOCK_LEN,
	sizeof(aesni_ctx),
	cipher_aesni_cbc_probe,
	cipher_aesni_cbc_setkey,
	cipher_aesni_cbc_encrypt,
	cipher_aesni_cbc_decrypt,
};

/**
 * --------------------------------------
 * AES-XTS in hardware (AES-NI)
 * --------------------------------------
 */

static int
cipher_aesni_xts_probe(const char *algo_name, const char *mode_name,
    int keysize_in_bits)
{
	if (aesni_disable)
		return (-1);

	if ((cpu_feature2 & CPUID2_AESNI) == 0)
		return (EINVAL);

	if ((strcmp(algo_name, "aes") == 0) &&
	    (strcmp(mode_name, "xts") == 0) &&
	    (keysize_in_bits == 256 || keysize_in_bits == 512))
		return (0);

	return (-1);
}

static int
cipher_aesni_xts_setkey(struct crypto_cipher_context *ctx,
    const uint8_t *keydata, int keylen_in_bytes)
{
	bzero(ctx, sizeof(*ctx));
	int rounds;

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

	uint8_t *enc_schedule = AESNI_ALIGNED_ENC_SCHEDULE(ctx, );
	uint8_t *dec_schedule = AESNI_ALIGNED_DEC_SCHEDULE(ctx, );
	uint8_t *xts_schedule = AESNI_ALIGNED_XTS_SCHEDULE(ctx, );

	AESNI_CTX(ctx).rounds = rounds;

	aesni_set_enckey(keydata, enc_schedule, rounds);
	aesni_set_deckey(enc_schedule, dec_schedule, rounds);
	aesni_set_enckey(keydata + (keylen_in_bytes / 2), xts_schedule, rounds);

	return (0);
}

static int
cipher_aesni_xts_encrypt(const struct crypto_cipher_context *ctx, uint8_t *data,
    int datalen, struct crypto_cipher_iv *iv)
{
	if ((datalen % AES_BLOCK_LEN) != 0)
		return (EINVAL);

	const uint8_t *enc_schedule = AESNI_ALIGNED_ENC_SCHEDULE(ctx, const);
	const uint8_t *xts_schedule = AESNI_ALIGNED_XTS_SCHEDULE(ctx, const);

	KKASSERT_AESNI_ALIGNED(enc_schedule);
	KKASSERT_AESNI_ALIGNED(xts_schedule);

	aesni_encrypt_xts(AESNI_CTX(ctx).rounds, enc_schedule, xts_schedule,
	    datalen, data, data, AESNI_IV(iv));

	return (0);
}

static int
cipher_aesni_xts_decrypt(const struct crypto_cipher_context *ctx, uint8_t *data,
    int datalen, struct crypto_cipher_iv *iv)
{
	if ((datalen % AES_BLOCK_LEN) != 0)
		return (EINVAL);

	const uint8_t *dec_schedule = AESNI_ALIGNED_DEC_SCHEDULE(ctx, const);
	const uint8_t *xts_schedule = AESNI_ALIGNED_XTS_SCHEDULE(ctx, const);

	KKASSERT_AESNI_ALIGNED(dec_schedule);
	KKASSERT_AESNI_ALIGNED(xts_schedule);

	aesni_decrypt_xts(AESNI_CTX(ctx).rounds, dec_schedule, xts_schedule,
	    datalen, data, data, AESNI_IV(iv));

	return (0);
}

const struct crypto_cipher cipher_aesni_xts = {
	"aesni-xts",
	"AES-XTS w/ CPU AESNI instruction",
	AES_BLOCK_LEN,
	AES_BLOCK_LEN,
	sizeof(aesni_ctx),
	cipher_aesni_xts_probe,
	cipher_aesni_xts_setkey,
	cipher_aesni_xts_encrypt,
	cipher_aesni_xts_decrypt,
};

/**
 *
 */

static const struct crypto_cipher *crypto_ciphers[] = {

	&cipher_null,

	/* first probe AESNI, then fallback to software AES */
	&cipher_aesni_cbc, &cipher_aesni_xts,

	/* AES in software */
	&cipher_aes_cbc, &cipher_aes_xts
};

/**
 * --------------------------------------
 * --------------------------------------
 */

const struct crypto_cipher *
crypto_cipher_find(const char *algo_name, const char *mode_name,
    int keysize_in_bits)
{
	const struct crypto_cipher *cipher;
	size_t i;

	for (i = 0; i < nitems(crypto_ciphers); i++) {
		cipher = crypto_ciphers[i];
		if ((*cipher->probe)(algo_name, mode_name, keysize_in_bits) ==
		    0) {
			return cipher;
		}
	}

	return NULL;
}
