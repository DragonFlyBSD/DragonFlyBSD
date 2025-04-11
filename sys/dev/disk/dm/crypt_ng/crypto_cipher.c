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
#include <sys/malloc.h>
#include <sys/sysctl.h>

#include <machine/specialreg.h> /* for CPUID2_AESNI */

#include <dev/disk/dm/crypt_ng/crypto_cipher.h>

#include <crypto/aesni/aesni.h>
#include <crypto/rijndael/rijndael.h>

MALLOC_DEFINE(M_CRYPTOCIPHER, "crypto_cipher", "Crypto cipher session");

static int aesni_disable = 0;
// TUNABLE_INT("hw.aesni_disable", &aesni_disable);
SYSCTL_INT(_hw, OID_AUTO, aesni_disable, CTLFLAG_RW, &aesni_disable, 0,
    "Disable AESNI");

/**
 * --------------------------------------
 *  Cipher specification
 * --------------------------------------
 */

struct crypto_cipher_spec {
	const char *shortname;
	const char *description;
	uint16_t blocksize;
	uint16_t ivsize;
	uint16_t ctxsize;
	uint16_t ctxalign;

	int (*probe)(const char *algo_name, const char *mode_name,
	    int keysize_in_bits);

	int (*setkey)(void *ctx, const uint8_t *keydata, int keylen_in_bytes);

	void (*encrypt)(const void *ctx, uint8_t *data, int datalen,
	    struct crypto_cipher_iv *iv);

	void (*decrypt)(const void *ctx, uint8_t *data, int datalen,
	    struct crypto_cipher_iv *iv);
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
cipher_null_setkey(void *ctx __unused, const uint8_t *keydata __unused,
    int keylen_in_bytes __unused)
{
	return (0);
}

static void
cipher_null_encrypt(const void *ctx __unused, uint8_t *data __unused,
    int datalen __unused, struct crypto_cipher_iv *iv __unused)
{
}

static void
cipher_null_decrypt(const void *ctx __unused, uint8_t *data, int datalen,
    struct crypto_cipher_iv *iv __unused)
{
}

const struct crypto_cipher_spec cipher_null = {
	.shortname = "null",
	.description = "null",
	.blocksize = 1,
	.ivsize = 0,
	.ctxsize = 0,
	.ctxalign = 0,
	.probe = cipher_null_probe,
	.setkey = cipher_null_setkey,
	.encrypt = cipher_null_encrypt,
	.decrypt = cipher_null_decrypt,
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
aes_cbc_encrypt(const void *ctx, uint8_t *data, int datalen,
    struct crypto_cipher_iv *iv)
{
	encrypt_data_cbc(rijndael_encrypt_wrap, ctx, data, datalen,
	    AES_BLOCK_LEN, (uint8_t *)iv);
}

static void
aes_cbc_decrypt(const void *ctx, uint8_t *data, int datalen,
    struct crypto_cipher_iv *iv)
{
	decrypt_data_cbc(rijndael_decrypt_wrap, ctx, data, datalen,
	    AES_BLOCK_LEN, (uint8_t *)iv);
}

const struct crypto_cipher_spec cipher_aes_cbc = {
	.shortname = "aes-cbc",
	.description = "AES-CBC (Rijndael-128) in software",
	.blocksize = AES_BLOCK_LEN,
	.ivsize = AES_BLOCK_LEN,
	.ctxsize = sizeof(rijndael_ctx),
	/* there are no alignment requirements imposed by the algorithm,
		but using 16 can't do any harm either */
	.ctxalign = 16,
	.probe = aes_cbc_probe,
	.setkey = aes_cbc_setkey,
	.encrypt = aes_cbc_encrypt,
	.decrypt = aes_cbc_decrypt,
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

static void
aes_xts_encrypt(const void *_ctx, uint8_t *data, int datalen,
    struct crypto_cipher_iv *_iv)
{
	uint8_t block[AES_XTS_BLOCK_LEN];
	uint8_t *iv = _iv->_iv._aes_xts;
	const struct aes_xts_ctx *ctx = _ctx;

	aes_xts_reinit(ctx, iv);
	for (int i = 0; i < datalen; i += AES_XTS_BLOCK_LEN) {
		aes_xts_crypt_block(&ctx->key1, data + i, iv,
		    rijndael_encrypt_wrap, block);
	}
	explicit_bzero(block, sizeof(block));
}

static void
aes_xts_decrypt(const void *_ctx, uint8_t *data, int datalen,
    struct crypto_cipher_iv *_iv)
{
	uint8_t block[AES_XTS_BLOCK_LEN];
	uint8_t *iv = _iv->_iv._aes_xts;
	const struct aes_xts_ctx *ctx = _ctx;

	aes_xts_reinit(ctx, iv);
	for (int i = 0; i < datalen; i += AES_XTS_BLOCK_LEN) {
		aes_xts_crypt_block(ctx, data + i, iv, rijndael_decrypt_wrap,
		    block);
	}

	explicit_bzero(block, sizeof(block));
}

const struct crypto_cipher_spec cipher_aes_xts = {
	.shortname = "aes-xts",
	.description = "AES-XTS (in software)",
	.blocksize = AES_XTS_BLOCK_LEN,
	.ivsize = 16,
	.ctxsize = sizeof(struct aes_xts_ctx),
	/* there are no alignment requirements imposed by the algorithm,
		but using 16 can't do any harm either */
	.ctxalign = 16,
	.probe = aes_xts_probe,
	.setkey = aes_xts_setkey,
	.encrypt = aes_xts_encrypt,
	.decrypt = aes_xts_decrypt,
};

/**
 * --------------------------------------
 * AES-CBC in hardware (AES-NI)
 * --------------------------------------
 */

struct aesni_ctx {
	uint8_t enc_schedule[AES_SCHED_LEN] __aligned(AESNI_ALIGN);
	uint8_t dec_schedule[AES_SCHED_LEN] __aligned(AESNI_ALIGN);
	uint8_t xts_schedule[AES_SCHED_LEN] __aligned(AESNI_ALIGN);
	int rounds;
};

#define AESNI_IV(iv) (iv->_iv._aesni)

#define ASSERT_AESNI_ALIGNED(ptr)                                   \
	KKASSERT((((uintptr_t)ptr) % AESNI_ALIGN) == 0);            \
	if (__predict_false((((uintptr_t)ptr) % AESNI_ALIGN) != 0)) \
		panic("AESNI misaligned");

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
cipher_aesni_cbc_encrypt(const void *_ctx, uint8_t *data, int datalen,
    struct crypto_cipher_iv *iv)
{
	const struct aesni_ctx *ctx = _ctx;

	aesni_encrypt_cbc(ctx->rounds, ctx->enc_schedule, datalen, data, data,
	    AESNI_IV(iv));
}

static void
cipher_aesni_cbc_decrypt(const void *_ctx, uint8_t *data, int datalen,
    struct crypto_cipher_iv *iv)
{
	const struct aesni_ctx *ctx = _ctx;

	aesni_decrypt_cbc(ctx->rounds, ctx->dec_schedule, datalen, data,
	    AESNI_IV(iv));
}

const struct crypto_cipher_spec cipher_aesni_cbc = {
	.shortname = "aesni-cbc",
	.description = "AES-CBC w/ CPU AESNI instruction",
	.blocksize = AES_BLOCK_LEN,
	.ivsize = AES_BLOCK_LEN,
	.ctxsize = sizeof(struct aesni_ctx),
	.ctxalign = AESNI_ALIGN,
	.probe = cipher_aesni_cbc_probe,
	.setkey = cipher_aesni_cbc_setkey,
	.encrypt = cipher_aesni_cbc_encrypt,
	.decrypt = cipher_aesni_cbc_decrypt,
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
cipher_aesni_xts_encrypt(const void *_ctx, uint8_t *data, int datalen,
    struct crypto_cipher_iv *iv)
{
	const struct aesni_ctx *ctx = _ctx;

	aesni_encrypt_xts(ctx->rounds, ctx->enc_schedule, ctx->xts_schedule,
	    datalen, data, data, AESNI_IV(iv));
}

static void
cipher_aesni_xts_decrypt(const void *_ctx, uint8_t *data, int datalen,
    struct crypto_cipher_iv *iv)
{
	const struct aesni_ctx *ctx = _ctx;

	aesni_decrypt_xts(ctx->rounds, ctx->dec_schedule, ctx->xts_schedule,
	    datalen, data, data, AESNI_IV(iv));
}

const struct crypto_cipher_spec cipher_aesni_xts = {
	.shortname = "aesni-xts",
	.description = "AES-XTS w/ CPU AESNI instruction",
	.blocksize = AES_BLOCK_LEN,
	.ivsize = AES_BLOCK_LEN,
	.ctxsize = sizeof(struct aesni_ctx),
	.ctxalign = AESNI_ALIGN,
	.probe = cipher_aesni_xts_probe,
	.setkey = cipher_aesni_xts_setkey,
	.encrypt = cipher_aesni_xts_encrypt,
	.decrypt = cipher_aesni_xts_decrypt,
};

/**
 *
 */

static crypto_cipher_t crypto_ciphers[] = {
	&cipher_null,

	/* first probe AESNI, then fallback to software AES */
	&cipher_aesni_cbc,
	&cipher_aesni_xts,

	/* AES in software */
	&cipher_aes_cbc,
	&cipher_aes_xts,
};

/**
 * --------------------------------------
 * --------------------------------------
 */

crypto_cipher_t
crypto_cipher_find(const char *algo_name, const char *mode_name,
    int keysize_in_bits)
{
	crypto_cipher_t cipher;
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

const char *
crypto_cipher_get_description(crypto_cipher_t cipher)
{
	if (cipher == NULL)
		return NULL;

	return cipher->description;
}

static __inline bool
is_ptr_aligned(void *ptr, int alignment)
{
	return (((uintptr_t)ptr % alignment) == 0);
}

static __inline void *
kmalloc_aligned(int size, int alignment, void **origptr)
{
	void *ptr;

	ptr = kmalloc(size, M_CRYPTOCIPHER, M_WAITOK);

	if (is_ptr_aligned(ptr, alignment)) {
		*origptr = ptr;
		return ptr;
	}

	kfree(ptr, M_CRYPTOCIPHER);

	ptr = kmalloc(size + alignment, M_CRYPTOCIPHER, M_WAITOK);

	uintptr_t offset = alignment - ((uintptr_t)ptr % alignment);

	KKASSERT(offset < alignment);

	*origptr = ptr;
	return ((uint8_t *)ptr + offset);
}

int
crypto_cipher_initsession(crypto_cipher_t cipher,
    crypto_cipher_session_t *session)
{
	if (session == NULL)
		return (EINVAL);

	bzero(session, sizeof(crypto_cipher_session_t));

	if (cipher->ctxsize > 0) {
		session->context = kmalloc_aligned(cipher->ctxsize,
		    cipher->ctxalign, &session->origptr);

		if (session->context == NULL)
			return (ENOMEM);

		KKASSERT(is_ptr_aligned(session->context, cipher->ctxalign));

		/**
		 * Fill context with random data, just in case
		 * someone forgets to initialize it.
		 */
		karc4random_buf(session->context, cipher->ctxsize);
	} else {
		session->context = NULL;
		session->origptr = NULL;
	}

	session->cipher = cipher;

	return (0);
}

int
crypto_cipher_freesession(crypto_cipher_session_t *session)
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
		kfree(session->origptr, M_CRYPTOCIPHER);
	}

	bzero(session, sizeof(*session));

	return (0);
}

int
crypto_cipher_setkey(crypto_cipher_session_t *session, const uint8_t *keydata,
    int keylen_in_bytes)
{
	return session->cipher->setkey(session->context, keydata,
	    keylen_in_bytes);
}

int
crypto_cipher_encrypt(const crypto_cipher_session_t *session, uint8_t *data,
    int datalen, struct crypto_cipher_iv *iv)
{
	if ((datalen % session->cipher->blocksize) != 0)
		return (EINVAL);

	session->cipher->encrypt(session->context, data, datalen, iv);

	return (0);
}

int
crypto_cipher_decrypt(const crypto_cipher_session_t *session, uint8_t *data,
    int datalen, struct crypto_cipher_iv *iv)
{
	if ((datalen % session->cipher->blocksize) != 0)
		return (EINVAL);

	session->cipher->decrypt(session->context, data, datalen, iv);

	return (0);
}

int
crypto_cipher_crypt(const crypto_cipher_session_t *session, uint8_t *data,
    int datalen, struct crypto_cipher_iv *iv, crypto_cipher_mode mode)
{
	switch (mode) {
	case CRYPTO_CIPHER_ENCRYPT:
		return crypto_cipher_encrypt(session, data, datalen, iv);
	case CRYPTO_CIPHER_DECRYPT:
		return crypto_cipher_decrypt(session, data, datalen, iv);
	default:
		return EINVAL;
	}
}
