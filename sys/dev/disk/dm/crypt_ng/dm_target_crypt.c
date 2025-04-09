/*
 * Copyright (c) 2010, 2024, 2025 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com> and
 * Michael Neumann <mneumann@ntecs.de>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * This file implements initial version of device-mapper crypt target.
 */

#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/bio.h>
#include <sys/kerneldump.h>
#include <sys/malloc.h>
#include <sys/md5.h>
#include <crypto/sha1.h>
#include <crypto/sha2/sha2.h>
#include <crypto/rmd160/rmd160.h>
#include <dev/disk/dm/crypt_ng/crypto_cipher.h>
#include <dev/disk/dm/dm.h>

#include <sys/proc.h>
#include <sys/types.h>
#include <sys/mpipe.h>

MALLOC_DEFINE(M_DMCRYPT, "dm_crypt", "Device Mapper Target Crypt");


struct target_crypt_config;

typedef void ivgen_t(struct target_crypt_config *, u_int8_t *, size_t, off_t);

typedef int ivgen_ctor_t(struct target_crypt_config *, char *, void **);
typedef int ivgen_dtor_t(struct target_crypt_config *, void *);


struct iv_generator {
	const char	*name;
	ivgen_ctor_t	*ctor;
	ivgen_dtor_t	*dtor;
	ivgen_t		*gen_iv;
};

struct essiv_ivgen_priv {
	struct crypto_cipher_context crypto_context;
	const struct crypto_cipher  *crypto_cipher;
	size_t			keyhash_len;
	u_int8_t		crypto_keyhash[SHA512_DIGEST_LENGTH];
};

typedef struct target_crypt_config {
	size_t	params_len;
	dm_pdev_t *pdev;
	char	*status_str;
	const struct crypto_cipher *crypto_cipher;
	int	crypto_klen;
	u_int8_t	crypto_key[512>>3];
	struct crypto_cipher_context	crypto_context;

	u_int64_t	block_offset;
	int64_t		iv_offset;
	SHA512_CTX	essivsha512_ctx;

	struct iv_generator	*ivgen;
	void	*ivgen_priv;

	struct malloc_pipe	read_mpipe;
	struct malloc_pipe	write_mpipe;

} dm_target_crypt_config_t;

struct dmtc_dump_helper {
	dm_target_crypt_config_t *priv;
	void *data;
	size_t length;
	off_t offset;

	u_char space[65536];
};

static void dmtc_init_mpipe(struct target_crypt_config *priv);
static void dmtc_destroy_mpipe(struct target_crypt_config *priv);

static const struct crypto_cipher *
dmtc_find_crypto_cipher(const char *crypto_alg, const char *crypto_mode,
    int klen_in_bits);

#define DMTC_BUF_SIZE (MAXPHYS)

static void dmtc_bio_read_done(struct bio *bio);

static void dmtc_bio_read_decrypt_start(struct bio *bio);

static void dmtc_bio_read_decrypt_retry(void *arg1, void *arg2);

static void dmtc_bio_read_decrypt(struct bio *bio, uint8_t *data_buf,
    size_t data_buf_sz);

static void dmtc_bio_write_encrypt_start(struct bio *bio);

static void dmtc_bio_write_encrypt_retry(void *arg1, void *arg2);

static void dmtc_bio_write_encrypt(struct bio *bio, uint8_t *data_buf,
    size_t data_buf_sz);

static void dmtc_bio_write_done(struct bio *bio);

static int dmtc_bio_encdec(dm_target_crypt_config_t *priv, uint8_t *data_buf,
    int bytes, off_t offset, crypto_cipher_blockfn_t blockfn);


static void dmtc_crypto_dump(dm_target_crypt_config_t *priv,
    struct dmtc_dump_helper *dump_helper);

static ivgen_ctor_t	essiv_ivgen_ctor;
static ivgen_dtor_t	essiv_ivgen_dtor;
static ivgen_t		essiv_ivgen;
static ivgen_t		plain_ivgen;
static ivgen_t		plain64_ivgen;

static struct iv_generator ivgens[] = {
	{ .name = "essiv", .ctor = essiv_ivgen_ctor, .dtor = essiv_ivgen_dtor,
	    .gen_iv = essiv_ivgen },
	{ .name = "plain", .ctor = NULL, .dtor = NULL, .gen_iv = plain_ivgen },
	{ .name = "plain64", .ctor = NULL, .dtor = NULL, .gen_iv = plain64_ivgen },
	{ NULL, NULL, NULL, NULL }
};

static __inline int
clamp_value(int value, int min, int max)
{
	if (value < min)
		return min;
	if (value > max)
		return max;
	return value;
}

static __inline int
dmtc_get_nmax(void)
{
	int nmax;
	nmax = (physmem * 2 / 1000 * PAGE_SIZE) /
		(2 * DMTC_BUF_SIZE) + 1;
	return clamp_value(nmax, 2, 8 + ncpus * 2);
}

static void
dmtc_zero_mpipe_buffer(void *buffer, void *priv __unused)
{
	explicit_bzero(buffer, DMTC_BUF_SIZE);
}

static void
dmtc_init_mpipe(struct target_crypt_config *priv)
{
	int nmax = dmtc_get_nmax();
	int nmax_read = nmax;
	int nmax_write = nmax;

	kprintf("dm_target_crypt: Setting %d mpipe read buffers\n", nmax_read);
	kprintf("dm_target_crypt: Setting %d mpipe write buffers\n", nmax_read);

	mpipe_init(&priv->read_mpipe, M_DMCRYPT, DMTC_BUF_SIZE,
		nmax_read, nmax_read, MPF_NOZERO | MPF_CALLBACK,
		NULL, dmtc_zero_mpipe_buffer, NULL);

	mpipe_init(&priv->write_mpipe, M_DMCRYPT, DMTC_BUF_SIZE,
		nmax_write, nmax_write, MPF_NOZERO | MPF_CALLBACK,
		NULL, dmtc_zero_mpipe_buffer, NULL);
}

static void
dmtc_destroy_mpipe(struct target_crypt_config *priv)
{
	mpipe_done(&priv->read_mpipe);
	mpipe_done(&priv->write_mpipe);
}

/*
 * Overwrite private information (in buf) to avoid leaking it
 */
static void
dmtc_crypto_clear(void *buf, size_t len)
{
	memset(buf, 0xFF, len);
	explicit_bzero(buf, len);
}

/*
 * ESSIV IV Generator Routines
 */
static int
essiv_ivgen_ctor(struct target_crypt_config *priv, char *iv_hash, void **p_ivpriv)
{
	struct essiv_ivgen_priv *ivpriv;
	u_int8_t crypto_keyhash[SHA512_DIGEST_LENGTH];
	unsigned int klen, hashlen;
	int error;

	klen = (priv->crypto_klen >> 3);

	if (iv_hash == NULL)
		return EINVAL;

	if (strcmp(iv_hash, "sha1") == 0) {
		SHA1_CTX ctx;

		hashlen = SHA1_RESULTLEN;
		SHA1Init(&ctx);
		SHA1Update(&ctx, priv->crypto_key, klen);
		SHA1Final(crypto_keyhash, &ctx);
	} else if (strcmp(iv_hash, "sha256") == 0) {
		SHA256_CTX ctx;

		hashlen = SHA256_DIGEST_LENGTH;
		SHA256_Init(&ctx);
		SHA256_Update(&ctx, priv->crypto_key, klen);
		SHA256_Final(crypto_keyhash, &ctx);
	} else if (strcmp(iv_hash, "sha384") == 0) {
		SHA384_CTX ctx;

		hashlen = SHA384_DIGEST_LENGTH;
		SHA384_Init(&ctx);
		SHA384_Update(&ctx, priv->crypto_key, klen);
		SHA384_Final(crypto_keyhash, &ctx);
	} else if (strcmp(iv_hash, "sha512") == 0) {
		SHA512_CTX ctx;

		hashlen = SHA512_DIGEST_LENGTH;
		SHA512_Init(&ctx);
		SHA512_Update(&ctx, priv->crypto_key, klen);
		SHA512_Final(crypto_keyhash, &ctx);
	} else if (strcmp(iv_hash, "md5") == 0) {
		MD5_CTX ctx;

		hashlen = MD5_DIGEST_LENGTH;
		MD5Init(&ctx);
		MD5Update(&ctx, priv->crypto_key, klen);
		MD5Final(crypto_keyhash, &ctx);
	} else if (strcmp(iv_hash, "rmd160") == 0 ||
		   strcmp(iv_hash, "ripemd160") == 0) {
		RMD160_CTX ctx;

		hashlen = 160/8;
		RMD160Init(&ctx);
		RMD160Update(&ctx, priv->crypto_key, klen);
		RMD160Final(crypto_keyhash, &ctx);
	} else {
		return EINVAL;
	}

	/* Convert hashlen to bits */
	hashlen <<= 3;

	ivpriv = kmalloc(sizeof(struct essiv_ivgen_priv), M_DMCRYPT,
			 M_WAITOK | M_ZERO);
	memcpy(ivpriv->crypto_keyhash, crypto_keyhash, sizeof(crypto_keyhash));
	ivpriv->keyhash_len = sizeof(crypto_keyhash);
	dmtc_crypto_clear(crypto_keyhash, sizeof(crypto_keyhash));


	/*
	 * XXX: in principle we also need to check if the block size of the
	 *	cipher is a valid iv size for the block cipher.
	 */

	ivpriv->crypto_cipher = priv->crypto_cipher;

	error = ivpriv->crypto_cipher->setkey(&ivpriv->crypto_context,
		(const uint8_t *)ivpriv->crypto_keyhash,
		hashlen / 8);

	if (error) {
		kprintf("dm_target_crypt: Error during crypto_newsession "
			"for essiv_ivgen, error = %d\n",
			error);
		dmtc_crypto_clear(ivpriv->crypto_keyhash, ivpriv->keyhash_len);
		kfree(ivpriv, M_DMCRYPT);
		return ENOTSUP;
	}

	*p_ivpriv = ivpriv;

	return 0;
}

static int
essiv_ivgen_dtor(struct target_crypt_config *priv, void *arg)
{
	struct essiv_ivgen_priv *ivpriv;

	ivpriv = (struct essiv_ivgen_priv *)arg;
	KKASSERT(ivpriv != NULL);

	dmtc_crypto_clear(&ivpriv->crypto_context, sizeof(ivpriv->crypto_context));

	dmtc_crypto_clear(ivpriv->crypto_keyhash, ivpriv->keyhash_len);
	kfree(ivpriv, M_DMCRYPT);

	return 0;
}

static void
essiv_ivgen(dm_target_crypt_config_t *priv, u_int8_t *iv,
	    size_t iv_len, off_t sector)
{
	struct essiv_ivgen_priv *ivpriv;
	int error;

	ivpriv = priv->ivgen_priv;
	KKASSERT(ivpriv != NULL);

	bzero(iv, iv_len);
	*((off_t *)iv) = htole64(sector + priv->iv_offset);

	struct crypto_cipher_iv iv2;
	bzero(&iv2, sizeof(iv2));

	error = ivpriv->crypto_cipher->encrypt(
			&ivpriv->crypto_context,
			(uint8_t*)iv,
			iv_len,
			&iv2
			);

	explicit_bzero(&iv2, sizeof(iv2));

	if (error)
		kprintf("dm_target_crypt: essiv_ivgen, error = %d\n", error);
}


static void
plain_ivgen(dm_target_crypt_config_t *priv, u_int8_t *iv,
	    size_t iv_len, off_t sector)
{
	bzero(iv, iv_len);
	*((uint32_t *)iv) = htole32((uint32_t)(sector + priv->iv_offset));
}

static void
plain64_ivgen(dm_target_crypt_config_t *priv, u_int8_t *iv,
    size_t iv_len, off_t sector)
{
	bzero(iv, iv_len);
	*((uint64_t *)iv) = htole64((uint64_t)(sector + priv->iv_offset));
}

/*
 * Init function called from dm_table_load_ioctl.
 * cryptsetup actually passes us this:
 * aes-cbc-essiv:sha256 7997f8af... 0 /dev/ad0s0a 8
 */
static int
hex2key(char *hex, size_t key_len, u_int8_t *key)
{
	char hex_buf[3];
	size_t key_idx;

	hex_buf[2] = 0;
	for (key_idx = 0; key_idx < key_len; ++key_idx) {
		hex_buf[0] = *hex++;
		hex_buf[1] = *hex++;
		key[key_idx] = (u_int8_t)strtoul(hex_buf, NULL, 16);
	}
	hex_buf[0] = 0;
	hex_buf[1] = 0;

	return 0;
}

/**
 * Map between dm_target_crypt algorithm naming and our own crypto/crypt_cipher
 * naming. It happens that they are identical, but it doesn't have to be this
 * way.
 */
static const struct crypto_cipher *
dmtc_find_crypto_cipher(const char *crypto_alg, const char *crypto_mode,
    int klen_in_bits)
{
#define ALGO_MODE_EQ(algo, mode) \
	((strcmp(crypto_alg, algo) == 0) && (strcmp(crypto_mode, mode) == 0))

	if (ALGO_MODE_EQ("aes", "cbc"))
		return crypto_cipher_find("aes", "cbc", klen_in_bits);

	if (ALGO_MODE_EQ("aes", "xts"))
		return crypto_cipher_find("aes", "xts", klen_in_bits);

	kprintf("dm_target_crypt: unsupported algo: %s and mode: %s\n",
	    crypto_alg, crypto_mode);

	return NULL;
}

static int
dm_target_crypt_init(dm_table_entry_t *table_en, int argc, char **argv)
{
	dm_target_crypt_config_t *priv;
	size_t len;
	char *crypto_alg, *crypto_mode, *iv_mode, *iv_opt, *hex_key, *dev;
	char *status_str;
	int i, klen_in_bits, error;
	uint64_t iv_offset, block_offset;

	if (argc != 5) {
		kprintf("dm_target_crypt: not enough arguments, "
			"need exactly 5\n");
		return EINVAL;
	}

	len = 0;
	for (i = 0; i < argc; i++) {
		len += strlen(argv[i]);
		len++;
	}
	/* len is strlen() of input string +1 */
	status_str = kmalloc(len, M_DMCRYPT, M_WAITOK);

	crypto_alg = strsep(&argv[0], "-");
	crypto_mode = strsep(&argv[0], "-");
	iv_opt = strsep(&argv[0], "-");
	iv_mode = strsep(&iv_opt, ":");
	hex_key = argv[1];
	iv_offset = strtouq(argv[2], NULL, 0);
	dev = argv[3];
	block_offset = strtouq(argv[4], NULL, 0);
	/* hex_key is specified in hex, so one hex char == 4 bits */
	klen_in_bits = strlen(hex_key) * 4;

#if 0
	kprintf("dm_target_crypt - new: dev=%s, crypto_alg=%s, crypto_mode=%s, "
		"iv_mode=%s, iv_opt=%s, key=%s, iv_offset=%ju, "
		"block_offset=%ju\n",
		dev, crypto_alg, crypto_mode, iv_mode, iv_opt, hex_key, iv_offset,
		block_offset);
#endif

	priv = kmalloc(sizeof(dm_target_crypt_config_t), M_DMCRYPT, M_WAITOK);

	/* Insert dmp to global pdev list */
	if ((priv->pdev = dm_pdev_insert(dev)) == NULL) {
		kprintf("dm_target_crypt: dm_pdev_insert failed\n");
		kfree(status_str, M_DMCRYPT);
		return ENOENT;
	}

	priv->crypto_cipher = dmtc_find_crypto_cipher(crypto_alg, crypto_mode, klen_in_bits);
	priv->crypto_klen = klen_in_bits;
	if (priv->crypto_cipher == NULL)
		goto notsup;

	kprintf("dm_target_crypt: using crypto_cipher: %s\n",
			priv->crypto_cipher->description);

	/* Save length of param string */
	priv->params_len = len;
	priv->block_offset = block_offset;
	priv->iv_offset = iv_offset - block_offset;

	dm_table_add_deps(table_en, priv->pdev);

	dm_table_init_target(table_en, priv);

	error = hex2key(hex_key, priv->crypto_klen / 8,
			(u_int8_t *)priv->crypto_key);

	if (error) {
		kprintf("dm_target_crypt: hex2key failed, "
			"invalid key format\n");
		goto notsup;
	}

	/* Handle cmd */
	for(i = 0; ivgens[i].name != NULL; i++) {
		if (strcmp(iv_mode, ivgens[i].name) == 0)
			break;
	}

	if (ivgens[i].name == NULL) {
		kprintf("dm_target_crypt: iv_mode='%s' unsupported\n",
			iv_mode);
		goto notsup;
	}

	/* Call our ivgen constructor */
	if (ivgens[i].ctor != NULL) {
		error = ivgens[i].ctor(priv, iv_opt,
		    &priv->ivgen_priv);
		if (error) {
			kprintf("dm_target_crypt: ctor for '%s' failed\n",
			    ivgens[i].name);
			goto notsup;
		}
	}

	priv->ivgen = &ivgens[i];

	error = priv->crypto_cipher->setkey(&priv->crypto_context,
		(const u_int8_t *)priv->crypto_key,
		priv->crypto_klen / 8);

	if (error) {
		kprintf("dm_target_crypt: Error during crypto_newsession, "
			"error = %d\n",
			error);
		goto notsup;
	}

	memset(hex_key, '0', strlen(hex_key));
	if (iv_opt) {
		ksprintf(status_str, "%s-%s-%s:%s %s %ju %s %ju",
		    crypto_alg, crypto_mode, iv_mode, iv_opt,
		    hex_key, iv_offset, dev, block_offset);
	} else {
		ksprintf(status_str, "%s-%s-%s %s %ju %s %ju",
		    crypto_alg, crypto_mode, iv_mode,
		    hex_key, iv_offset, dev, block_offset);
	}
	priv->status_str = status_str;

	/* Initialize mpipes */
	dmtc_init_mpipe(priv);

	return 0;

notsup:
	kprintf("dm_target_crypt: ENOTSUP\n");
	kfree(status_str, M_DMCRYPT);
	return ENOTSUP;
}

/* Table routine called to get params string. */
static char *
dm_target_crypt_table(void *target_config)
{
	dm_target_crypt_config_t *priv;
	char *params;

	priv = target_config;

	params = dm_alloc_string(DM_MAX_PARAMS_SIZE);

	ksnprintf(params, DM_MAX_PARAMS_SIZE, "%s",
	    priv->status_str);

	return params;
}

static int
dm_target_crypt_destroy(dm_table_entry_t *table_en)
{
	dm_target_crypt_config_t *priv;

	/*
	 * Disconnect the crypt config before unbusying the target.
	 */
	priv = table_en->target_config;
	if (priv == NULL)
		return 0;
	dm_pdev_decr(priv->pdev);

	dmtc_destroy_mpipe(priv);

	/*
	 * Clean up the crypt config
	 *
	 * Overwrite the private information before freeing memory to
	 * avoid leaking it.
	 */
	if (priv->status_str) {
		dmtc_crypto_clear(priv->status_str, strlen(priv->status_str));
		kfree(priv->status_str, M_DMCRYPT);
	}

	if ((priv->ivgen) && (priv->ivgen->dtor != NULL)) {
		priv->ivgen->dtor(priv, priv->ivgen_priv);
	}

	dmtc_crypto_clear(priv, sizeof(dm_target_crypt_config_t));
	kfree(priv, M_DMCRYPT);

	return 0;
}

/************************************************************************
 *			STRATEGY SUPPORT FUNCTIONS			*
 ************************************************************************
 *
 * READ PATH:	doio -> bio_read_done -> bio_read_decrypt
 * WRITE PATH:	bio_write_encrypt -> doio -> bio_write_done
 */

/**
 * Usage of the "struct bio" bio_caller_infoX fields:
 *
 * bio_caller_info1:
 * 	- always points to "priv"
 * bio_caller_info2:
 * 	- used to chain bio requests. points to next enqueued bio request
 * 	  (only within bio request queue)
 *	- orig b_data pointer (within write path)
 * bio_caller_info3:
 * 	- points to the mpipe
 */

/*
 * Start IO operation, called from dmstrategy routine.
 */
static int
dm_target_crypt_strategy(dm_table_entry_t *table_en, struct buf *bp)
{
	struct bio *bio;

	dm_target_crypt_config_t *priv;
	priv = table_en->target_config;

	/* Get rid of stuff we can't really handle */
	if ((bp->b_cmd == BUF_CMD_READ) ||
	    (bp->b_cmd == BUF_CMD_WRITE)) {
		if (((bp->b_bcount % DEV_BSIZE) != 0) ||
		    (bp->b_bcount == 0)) {
			kprintf(
			    "dm_target_crypt_strategy: can't really "
			    "handle bp->b_bcount = %d\n",
			    bp->b_bcount);
			bp->b_error = EINVAL;
			bp->b_flags |= B_ERROR | B_INVAL;
			biodone(&bp->b_bio1);
			return 0;
		}
	}

	switch (bp->b_cmd) {
	case BUF_CMD_READ:
		bio = push_bio(&bp->b_bio1);
		bio->bio_offset = bp->b_bio1.bio_offset +
		    priv->block_offset * DEV_BSIZE;
		bio->bio_caller_info1.ptr = priv;
		bio->bio_done = dmtc_bio_read_done;
		vn_strategy(priv->pdev->pdev_vnode, bio);
		break;
	case BUF_CMD_WRITE:
		bio = push_bio(&bp->b_bio1);
		bio->bio_offset = bp->b_bio1.bio_offset +
		    priv->block_offset * DEV_BSIZE;
		bio->bio_caller_info1.ptr = priv;
		dmtc_bio_write_encrypt_start(bio);
		break;
	default:
		vn_strategy(priv->pdev->pdev_vnode, &bp->b_bio1);
		break;
	}
	return 0;
}

/************************************************************************
 * READ PATH
 ************************************************************************
 *
 * DO IO -> dmtc_bio_read_done -> dmtc_bio_read_decrypt_start ->
 * (dmtc_bio_read_decrypt_retry) -> dmtc_bio_read_decrypt -> COMPLETE
 *
 */

/*
 * Called after read BIO completes
 */
static void
dmtc_bio_read_done(struct bio *bio)
{
	struct bio *obio;

	/*
	 * If a read error occurs we shortcut the operation, otherwise
	 * go on to stage 2 (decrypt).
	 */
	if (bio->bio_buf->b_flags & B_ERROR) {
		obio = pop_bio(bio);
		biodone(obio);
	} else {
		dmtc_bio_read_decrypt_start(bio);
	}
}

/**
 * Starts decryption by allocating a buffer.
 *
 * If allocation fails, dmtc_bio_read_decrypt_retry is called.
 */
void
dmtc_bio_read_decrypt_start(struct bio *bio)
{
	dm_target_crypt_config_t *priv;
	uint8_t *data_buf;

	priv = bio->bio_caller_info1.ptr;

	KKASSERT(bio->bio_buf->b_cmd == BUF_CMD_READ);

	data_buf = mpipe_alloc_callback(&priv->read_mpipe,
	    dmtc_bio_read_decrypt_retry, bio, NULL);

	if (data_buf == NULL)
		return;

	bio->bio_caller_info3.ptr = &priv->read_mpipe;
	dmtc_bio_read_decrypt(bio, data_buf, DMTC_BUF_SIZE);
}

/**
 * Retries the allocation.
 */
void
dmtc_bio_read_decrypt_retry(void *arg1, void *arg2 __unused)
{
	dmtc_bio_read_decrypt_start(arg1);
}

/**
 * Decrypts the buffer, releases the intermediate mpipe buffer and completes the
 * bio request.
 */
void
dmtc_bio_read_decrypt(struct bio *bio, uint8_t *data_buf, size_t data_buf_sz)
{
	dm_target_crypt_config_t *priv;
	struct malloc_pipe *mpipe;

	priv = bio->bio_caller_info1.ptr;
	mpipe = bio->bio_caller_info3.ptr;

	/*
	 * Note: b_resid no good after read I/O, it will be 0, use
	 *	 b_bcount.
	 */
	int bytes = bio->bio_buf->b_bcount;

	if (__predict_false(data_buf_sz < bytes))
		panic("dmtc: Allocated data buffer is too small");

	/*
	 * Unconditionally copy in data. Never decrypt in place!
	 *
	 * For reads with bogus page we can't decrypt in place as stuff
	 * can get ripped out from under us.
	 */
	memcpy(data_buf, bio->bio_buf->b_data, bytes);

	bio->bio_buf->b_error = dmtc_bio_encdec(priv, data_buf, bytes,
	    bio->bio_offset, priv->crypto_cipher->decrypt);

	if (bio->bio_buf->b_error) {
		kprintf("dm_target_crypt: dmtc_bio_read_decrypt error = %d\n",
		    bio->bio_buf->b_error);

		bio->bio_buf->b_flags |= B_ERROR;
	} else {
		memcpy(bio->bio_buf->b_data, data_buf, bytes);
	}
#if 0
	else if (bio->bio_buf->b_flags & B_HASBOGUS) {
		memcpy(bio->bio_buf->b_data, dmtc->data_buf,
		       bio->bio_buf->b_bcount);
	}
#endif

	struct bio *obio = pop_bio(bio);
	biodone(obio);

	if (mpipe)
		mpipe_free(mpipe, data_buf);
}

/************************************************************************
 * WRITE PATH
 ************************************************************************
 *
 * dmtc_bio_write_encrypt_start -> (dmtc_bio_write_encrypt_retry) ->
 * dmtc_bio_write_encrypt -> DO IO -> dmtc_bio_write_done -> COMPLETE
 *
 */

/**
 * Allocates a mpipe buffer in order to proceed with encryption.
 *
 * If that fails, dmtc_bio_write_encrypt_retry will be called.
 */
void
dmtc_bio_write_encrypt_start(struct bio *bio)
{
	dm_target_crypt_config_t *priv;
	uint8_t *data_buf;

	priv = bio->bio_caller_info1.ptr;

	KKASSERT(bio->bio_buf->b_cmd == BUF_CMD_WRITE);

	data_buf = mpipe_alloc_callback(&priv->write_mpipe,
	    dmtc_bio_write_encrypt_retry, bio, NULL);

	if (data_buf == NULL)
		return;

	bio->bio_caller_info3.ptr = &priv->write_mpipe;
	dmtc_bio_write_encrypt(bio, data_buf, DMTC_BUF_SIZE);
}

/**
 * Retries the allocation.
 */
void
dmtc_bio_write_encrypt_retry(void *arg1, void *arg2 __unused)
{
	dmtc_bio_write_encrypt_start(arg1);
}

/**
 *
 * Encrypts the buffer and sends a write request for the encrypted buffer to the
 * underlying device via vn_strategy. Once this completes, dmtc_bio_write_done
 * will be called.
 */
void
dmtc_bio_write_encrypt(struct bio *bio, uint8_t *data_buf, size_t data_buf_sz)
{
	dm_target_crypt_config_t *priv = bio->bio_caller_info1.ptr;

	/*
	 * Use b_bcount for consistency
	 */
	int bytes = bio->bio_buf->b_bcount;

	if (__predict_false(data_buf_sz < bytes))
		panic("dmtc: Allocated data buffer is too small");

	memcpy(data_buf, bio->bio_buf->b_data, bytes);

	bio->bio_buf->b_error = dmtc_bio_encdec(priv, data_buf, bytes,
	    bio->bio_offset, priv->crypto_cipher->encrypt);

	if (bio->bio_buf->b_error) {
		kprintf("dm_target_crypt: dmtc_bio_write_encrypt error = %d\n",
		    bio->bio_buf->b_error);

		mpipe_free(&priv->write_mpipe, data_buf);
		bio->bio_buf->b_flags |= B_ERROR;
		struct bio *obio = pop_bio(bio);
		biodone(obio);
	} else {
		bio->bio_caller_info2.ptr = bio->bio_buf->b_data; /* orig_buf */
		bio->bio_buf->b_data = data_buf;
		bio->bio_done = dmtc_bio_write_done;

		/*
		 * Note: We just copy back data_buf to bio->bio_buf->b_data
		 * and then call vn_stategy() on it.
		 *
		 * We have to allocate our own buffer, copy bio->bio_buf->b_data
		 * into it, modify the copy, rewire bio->bio_buf->b_data to our
		 * own buffer, call vn_stategy() and once it finished with
		 * dmtc_bio_write_done(), free our buffer.
		 */
		vn_strategy(priv->pdev->pdev_vnode, bio);
	}
}

/**
 * The write completed, so releases the intermediate buffer.
 */
void
dmtc_bio_write_done(struct bio *bio)
{
	dm_target_crypt_config_t *priv;
	uint8_t *data_buf;
	struct malloc_pipe *mpipe;

	priv = bio->bio_caller_info1.ptr;
	data_buf = bio->bio_buf->b_data;
	mpipe = bio->bio_caller_info3.ptr;

	if (mpipe)
		mpipe_free(mpipe, data_buf);

	// Restore original bio buffer
	bio->bio_buf->b_data = bio->bio_caller_info2.ptr;

	struct bio *obio = pop_bio(bio);
	biodone(obio);
}

/************************************************************************
 * COMMON
 ************************************************************************/

/**
 * Encrypts or decrypts `data_buf`.
 */
int
dmtc_bio_encdec(dm_target_crypt_config_t *priv, uint8_t *data_buf, int bytes,
    off_t offset, crypto_cipher_blockfn_t blockfn)
{
	struct crypto_cipher_iv iv;
	int sectors = bytes / DEV_BSIZE;    /* Number of sectors */
	off_t isector = offset / DEV_BSIZE; /* ivgen salt base? */
	int error = 0;

	KKASSERT((sectors * DEV_BSIZE) == bytes);

	for (int i = 0; i < sectors; i++) {
		/*
		 * Note: last argument is used to generate salt(?) and
		 * is a 64 bit value, but the original code passed an
		 *	 int.  Changing it now will break pre-existing
		 *	 crypt volumes.
		 */
		priv->ivgen->gen_iv(priv, (uint8_t *)&iv, sizeof(iv),
		    isector + i);

		error = blockfn(&priv->crypto_context, data_buf + i * DEV_BSIZE,
		    DEV_BSIZE, &iv);

		if (error) {
			break;
		}
	}

	// TODO: required?
	explicit_bzero(&iv, sizeof(iv));

	return (error);
}
/* DUMPING MAGIC */

extern int tsleep_crypto_dump;

static int
dm_target_crypt_dump(dm_table_entry_t *table_en, void *data, size_t length, off_t offset)
{
	static struct dmtc_dump_helper dump_helper;
	dm_target_crypt_config_t *priv;
	static int first_call = 1;

	priv = table_en->target_config;

	if (first_call) {
		first_call = 0;
		dump_reactivate_cpus();
	}

	/* Magically enable tsleep */
	tsleep_crypto_dump = 1;

	/*
	 * 0 length means flush buffers and return
	 */
	if (length == 0) {
		if (priv->pdev->pdev_vnode->v_rdev == NULL) {
			tsleep_crypto_dump = 0;
			return ENXIO;
		}
		dev_ddump(priv->pdev->pdev_vnode->v_rdev,
		    data, 0, offset, 0);
		tsleep_crypto_dump = 0;
		return 0;
	}

	bzero(&dump_helper, sizeof(dump_helper));
	dump_helper.priv = priv;
	dump_helper.data = data;
	dump_helper.length = length;
	dump_helper.offset = offset +
	    priv->block_offset * DEV_BSIZE;
	dmtc_crypto_dump(priv, &dump_helper);

	dump_helper.offset = dm_pdev_correct_dump_offset(priv->pdev,
	    dump_helper.offset);

	dev_ddump(priv->pdev->pdev_vnode->v_rdev,
	    dump_helper.space, 0, dump_helper.offset,
	    dump_helper.length);

	tsleep_crypto_dump = 0;
	return 0;
}

static void
dmtc_crypto_dump(dm_target_crypt_config_t *priv, struct dmtc_dump_helper *dump_helper)
{
	int bytes = dump_helper->length;

	KKASSERT(dump_helper->length <= 65536);

	memcpy(dump_helper->space, dump_helper->data, bytes);

	int error = dmtc_bio_encdec(priv, dump_helper->space, bytes, dump_helper->offset,
			priv->crypto_cipher->encrypt);
	if (error != 0) {
		kprintf("dm_target_crypt: dmtc_crypto_dump = %d\n",
		error);
	}
}

static int
dmtc_mod_handler(module_t mod, int type, void *unused)
{
	dm_target_t *dmt = NULL;
	int err = 0;

	switch (type) {
	case MOD_LOAD:
		if ((dmt = dm_target_lookup("crypt")) != NULL) {
			kprintf("dm_target_crypt: Error: crypt target already defined\n");
			dm_target_unbusy(dmt);
			return EEXIST;
		}
		dmt = dm_target_alloc("crypt");
		dmt->version[0] = 1;
		dmt->version[1] = 6;
		dmt->version[2] = 0;
		dmt->init = &dm_target_crypt_init;
		dmt->destroy = &dm_target_crypt_destroy;
		dmt->strategy = &dm_target_crypt_strategy;
		dmt->table = &dm_target_crypt_table;
		dmt->dump = &dm_target_crypt_dump;

		err = dm_target_insert(dmt);
		if (!err)
			kprintf("dm_target_crypt: Successfully initialized\n");
		break;

	case MOD_UNLOAD:
		err = dm_target_remove("crypt");
		if (err == 0) {
			kprintf("dm_target_crypt: unloaded\n");
		}
		break;
	}

	return err;
}

DM_TARGET_MODULE(dm_target_crypt_ng, dmtc_mod_handler);
MODULE_DEPEND(dm_target_crypt_ng, crypto, 1, 1, 1);
