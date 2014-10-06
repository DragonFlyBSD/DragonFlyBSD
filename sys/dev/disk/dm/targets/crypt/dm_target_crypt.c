/*
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
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
#include <sys/types.h>
#include <sys/param.h>
#include <sys/endian.h>

#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/globaldata.h>
#include <sys/kerneldump.h>
#include <sys/malloc.h>
#include <sys/mpipe.h>
#include <sys/md5.h>
#include <sys/mutex2.h>
#include <sys/vnode.h>
#include <crypto/sha1.h>
#include <crypto/sha2/sha2.h>
#include <opencrypto/cryptodev.h>
#include <opencrypto/rmd160.h>
#include <machine/cpufunc.h>

#include <sys/ktr.h>

#include <dev/disk/dm/dm.h>
MALLOC_DEFINE(M_DMCRYPT, "dm_crypt", "Device Mapper Target Crypt");

KTR_INFO_MASTER(dmcrypt);

#if !defined(KTR_DMCRYPT)
#define KTR_DMCRYPT	KTR_ALL
#endif

KTR_INFO(KTR_DMCRYPT, dmcrypt, crypto_dispatch, 0,
    "crypto_dispatch(%p)", struct cryptop *crp);
KTR_INFO(KTR_DMCRYPT, dmcrypt, crypt_strategy, 0,
    "crypt_strategy(b_cmd = %d, bp = %p)", int cmd, struct buf *bp);
KTR_INFO(KTR_DMCRYPT, dmcrypt, crypto_write_start, 1,
    "crypto_write_start(crp = %p, bp = %p, sector = %d/%d)",
    struct cryptop *crp, struct buf *bp, int i, int sectors);
KTR_INFO(KTR_DMCRYPT, dmcrypt, crypto_cb_write_done, 1,
    "crypto_cb_write_done(crp = %p, bp = %p, n = %d)",
    struct cryptop *crp, struct buf *bp, int n);
KTR_INFO(KTR_DMCRYPT, dmcrypt, bio_write_done, 1,
    "bio_write_done(bp = %p)", struct buf *bp);
KTR_INFO(KTR_DMCRYPT, dmcrypt, crypto_write_retry, 1,
    "crypto_write_retry(crp = %p)", struct buf *bp);
KTR_INFO(KTR_DMCRYPT, dmcrypt, bio_read_done, 2,
    "bio_read_done(bp = %p)", struct buf *bp);
KTR_INFO(KTR_DMCRYPT, dmcrypt, crypto_read_start, 2,
    "crypto_read_start(crp = %p, bp = %p, sector = %d/%d)",
    struct cryptop *crp, struct buf *bp, int i, int sectors);
KTR_INFO(KTR_DMCRYPT, dmcrypt, crypto_cb_read_done, 2,
    "crypto_cb_read_done(crp = %p, bp = %p, n = %d)",
    struct cryptop *crp, struct buf *bp, int n);

struct target_crypt_config;

typedef void dispatch_t(void *);
typedef void ivgen_t(struct target_crypt_config *, u_int8_t *, size_t, off_t,
    void *);

typedef int ivgen_ctor_t(struct target_crypt_config *, char *, void **);
typedef int ivgen_dtor_t(struct target_crypt_config *, void *);

struct iv_generator {
	const char	*name;
	ivgen_ctor_t	*ctor;
	ivgen_dtor_t	*dtor;
	ivgen_t		*gen_iv;
};

struct essiv_ivgen_priv {
	struct cryptoini	crypto_session;
	struct objcache	*crp_crd_cache;
	u_int64_t	crypto_sid;
	size_t		keyhash_len;
	u_int8_t	crypto_keyhash[SHA512_DIGEST_LENGTH];
};

typedef struct target_crypt_config {
	size_t	params_len;
	dm_pdev_t *pdev;
	char	*status_str;
	int	crypto_alg;
	int	crypto_klen;
	u_int8_t	crypto_key[512>>3];

	u_int64_t	crypto_sid;
	u_int64_t	block_offset;
	int64_t		iv_offset;
	SHA512_CTX	essivsha512_ctx;

	struct cryptoini	crypto_session;

	struct iv_generator	*ivgen;
	void	*ivgen_priv;

	struct malloc_pipe	read_mpipe;
	struct malloc_pipe	write_mpipe;
} dm_target_crypt_config_t;

struct dmtc_helper {
	dm_target_crypt_config_t *priv;
	caddr_t	free_addr;
	caddr_t	orig_buf;
	caddr_t data_buf;
};

struct dmtc_dump_helper {
	dm_target_crypt_config_t *priv;
	void *data;
	size_t length;
	off_t offset;

	int sectors;
	int *ident;

	struct cryptodesc crd[128];
	struct cryptop crp[128];
	u_char space[65536];
};

#define DMTC_BUF_SIZE_WRITE \
    MAXPHYS + sizeof(struct dmtc_helper) + \
    MAXPHYS/DEV_BSIZE*(sizeof(struct cryptop) + sizeof(struct cryptodesc))
#define DMTC_BUF_SIZE_READ \
    sizeof(struct dmtc_helper) + \
    MAXPHYS/DEV_BSIZE*(sizeof(struct cryptop) + sizeof(struct cryptodesc))

static void dmtc_crypto_dispatch(void *arg);
static void dmtc_crypto_dump_start(dm_target_crypt_config_t *priv,
				struct dmtc_dump_helper *dump_helper);
static void dmtc_crypto_read_start(dm_target_crypt_config_t *priv,
				struct bio *bio);
static void dmtc_crypto_write_start(dm_target_crypt_config_t *priv,
				struct bio *bio);
static void dmtc_bio_read_done(struct bio *bio);
static void dmtc_bio_write_done(struct bio *bio);
static int dmtc_crypto_cb_dump_done(struct cryptop *crp);
static int dmtc_crypto_cb_read_done(struct cryptop *crp);
static int dmtc_crypto_cb_write_done(struct cryptop *crp);

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

struct objcache_malloc_args essiv_ivgen_malloc_args = {
		2*sizeof(void *) + (sizeof(struct cryptodesc) +
		sizeof(struct cryptop)), M_DMCRYPT };

static void
dmtc_init_mpipe(struct target_crypt_config *priv)
{
	int nmax;

	nmax = (physmem*2/1000*PAGE_SIZE)/(DMTC_BUF_SIZE_WRITE + DMTC_BUF_SIZE_READ) + 1;

	if (nmax < 2)
		nmax = 2;

	kprintf("dm_target_crypt: Setting min/max mpipe buffers: %d/%d\n", 2, nmax);

	mpipe_init(&priv->write_mpipe, M_DMCRYPT, DMTC_BUF_SIZE_WRITE,
		   2, nmax, MPF_NOZERO | MPF_CALLBACK, NULL, NULL, NULL);
	mpipe_init(&priv->read_mpipe, M_DMCRYPT, DMTC_BUF_SIZE_READ,
		   2, nmax, MPF_NOZERO | MPF_CALLBACK, NULL, NULL, NULL);
}

static void
dmtc_destroy_mpipe(struct target_crypt_config *priv)
{
	mpipe_done(&priv->write_mpipe);
	mpipe_done(&priv->read_mpipe);
}

/*
 * Overwrite private information (in buf) to avoid leaking it
 */
static void
dmtc_crypto_clear(void *buf, size_t len)
{
	memset(buf, 0xFF, len);
	bzero(buf, len);
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

	if (!strcmp(iv_hash, "sha1")) {
		SHA1_CTX ctx;

		hashlen = SHA1_RESULTLEN;
		SHA1Init(&ctx);
		SHA1Update(&ctx, priv->crypto_key, klen);
		SHA1Final(crypto_keyhash, &ctx);
	} else if (!strcmp(iv_hash, "sha256")) {
		SHA256_CTX ctx;

		hashlen = SHA256_DIGEST_LENGTH;
		SHA256_Init(&ctx);
		SHA256_Update(&ctx, priv->crypto_key, klen);
		SHA256_Final(crypto_keyhash, &ctx);
	} else if (!strcmp(iv_hash, "sha384")) {
		SHA384_CTX ctx;

		hashlen = SHA384_DIGEST_LENGTH;
		SHA384_Init(&ctx);
		SHA384_Update(&ctx, priv->crypto_key, klen);
		SHA384_Final(crypto_keyhash, &ctx);
	} else if (!strcmp(iv_hash, "sha512")) {
		SHA512_CTX ctx;

		hashlen = SHA512_DIGEST_LENGTH;
		SHA512_Init(&ctx);
		SHA512_Update(&ctx, priv->crypto_key, klen);
		SHA512_Final(crypto_keyhash, &ctx);
	} else if (!strcmp(iv_hash, "md5")) {
		MD5_CTX ctx;

		hashlen = MD5_DIGEST_LENGTH;
		MD5Init(&ctx);
		MD5Update(&ctx, priv->crypto_key, klen);
		MD5Final(crypto_keyhash, &ctx);
	} else if (!strcmp(iv_hash, "rmd160") ||
		   !strcmp(iv_hash, "ripemd160")) {
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

	ivpriv->crypto_session.cri_alg = priv->crypto_alg;
	ivpriv->crypto_session.cri_key = (u_int8_t *)ivpriv->crypto_keyhash;
	ivpriv->crypto_session.cri_klen = hashlen;
	ivpriv->crypto_session.cri_mlen = 0;
	ivpriv->crypto_session.cri_next = NULL;

	/*
	 * XXX: in principle we also need to check if the block size of the
	 *	cipher is a valid iv size for the block cipher.
	 */

	error = crypto_newsession(&ivpriv->crypto_sid,
				  &ivpriv->crypto_session,
				  CRYPTOCAP_F_SOFTWARE | CRYPTOCAP_F_HARDWARE);
	if (error) {
		kprintf("dm_target_crypt: Error during crypto_newsession "
			"for essiv_ivgen, error = %d\n",
			error);
		dmtc_crypto_clear(ivpriv->crypto_keyhash, ivpriv->keyhash_len);
		kfree(ivpriv, M_DMCRYPT);
		return ENOTSUP;
	}

	ivpriv->crp_crd_cache = objcache_create(
	    "dmcrypt-essiv-cache", 0, 0,
	    NULL, NULL, NULL,
	    objcache_malloc_alloc,
	    objcache_malloc_free,
	    &essiv_ivgen_malloc_args );

	*p_ivpriv = ivpriv;
	return 0;
}

static int 
essiv_ivgen_dtor(struct target_crypt_config *priv, void *arg)
{
	struct essiv_ivgen_priv *ivpriv;

	ivpriv = (struct essiv_ivgen_priv *)arg;
	KKASSERT(ivpriv != NULL);

	crypto_freesession(ivpriv->crypto_sid);

	objcache_destroy(ivpriv->crp_crd_cache);

	dmtc_crypto_clear(ivpriv->crypto_keyhash, ivpriv->keyhash_len);
	kfree(ivpriv, M_DMCRYPT);

	return 0;
}

static int
essiv_ivgen_done(struct cryptop *crp)
{
	struct essiv_ivgen_priv *ivpriv;
	void *free_addr;
	void *opaque;


	if (crp->crp_etype == EAGAIN)
		return crypto_dispatch(crp);

	if (crp->crp_etype != 0) {
		kprintf("dm_target_crypt: essiv_ivgen_done, "
			"crp->crp_etype = %d\n", crp->crp_etype);
	}

	free_addr = crp->crp_opaque;
	/*
	 * In-memory structure is:
	 * |  ivpriv  |  opaque  |     crp     |      crd      |
	 * | (void *) | (void *) |   (cryptop) |  (cryptodesc) |
	 */
	ivpriv = *((struct essiv_ivgen_priv **)crp->crp_opaque);
	crp->crp_opaque += sizeof(void *);
	opaque = *((void **)crp->crp_opaque);

	objcache_put(ivpriv->crp_crd_cache, free_addr);
	dmtc_crypto_dispatch(opaque);
	return 0;
}

static void
essiv_ivgen(dm_target_crypt_config_t *priv, u_int8_t *iv,
	    size_t iv_len, off_t sector, void *opaque)
{
	struct essiv_ivgen_priv *ivpriv;
	struct cryptodesc *crd;
	struct cryptop *crp;
	caddr_t space, alloc_addr;
	int error;

	ivpriv = priv->ivgen_priv;
	KKASSERT(ivpriv != NULL);

	/*
	 * In-memory structure is:
	 * |  ivpriv  |  opaque  |     crp     |      crd      |
	 * | (void *) | (void *) |   (cryptop) |  (cryptodesc) |
	 */
	alloc_addr = space = objcache_get(ivpriv->crp_crd_cache, M_WAITOK);
	*((struct essiv_ivgen_priv **)space) = ivpriv;
	space += sizeof(void *);
	*((void **)space) = opaque;
	space += sizeof(void *);
	crp = (struct cryptop *)space;
	space += sizeof(struct cryptop);
	crd = (struct cryptodesc *)space;

	bzero(iv, iv_len);
	bzero(crd, sizeof(struct cryptodesc));
	bzero(crp, sizeof(struct cryptop));
	*((off_t *)iv) = htole64(sector + priv->iv_offset);
	crp->crp_buf = (caddr_t)iv;

	crp->crp_sid = ivpriv->crypto_sid;
	crp->crp_ilen = crp->crp_olen = iv_len;

	crp->crp_opaque = alloc_addr;

	crp->crp_callback = essiv_ivgen_done;

	crp->crp_desc = crd;
	crp->crp_etype = 0;
	crp->crp_flags = CRYPTO_F_CBIFSYNC | CRYPTO_F_REL | CRYPTO_F_BATCH;

	crd->crd_alg = priv->crypto_alg;
#if 0
	crd->crd_key = (caddr_t)priv->crypto_keyhash;
	crd->crd_klen = priv->crypto_klen;
#endif

	bzero(crd->crd_iv, sizeof(crd->crd_iv));

	crd->crd_skip = 0;
	crd->crd_len = iv_len;
	crd->crd_flags = CRD_F_IV_EXPLICIT | CRD_F_IV_PRESENT;
	crd->crd_flags |= CRD_F_ENCRYPT;
	crd->crd_next = NULL;

	error = crypto_dispatch(crp);
	if (error)
		kprintf("dm_target_crypt: essiv_ivgen, error = %d\n", error);
}


static void
plain_ivgen(dm_target_crypt_config_t *priv, u_int8_t *iv,
	    size_t iv_len, off_t sector, void *opaque)
{
	bzero(iv, iv_len);
	*((uint32_t *)iv) = htole32((uint32_t)(sector + priv->iv_offset));
	dmtc_crypto_dispatch(opaque);
}

static void
plain64_ivgen(dm_target_crypt_config_t *priv, u_int8_t *iv,
    size_t iv_len, off_t sector, void *opaque)
{
	bzero(iv, iv_len);
	*((uint64_t *)iv) = htole64((uint64_t)(sector + priv->iv_offset));
	dmtc_crypto_dispatch(opaque);
}

#if 0
static void
geli_ivgen(dm_target_crypt_config_t *priv, u_int8_t *iv,
	   size_t iv_len, off_t sector, void *opaque)
{

	SHA512_CTX	ctx512;
	u_int8_t	md[SHA512_DIGEST_LENGTH]; /* Max. Digest Size */

	memcpy(&ctx512, &priv->essivsha512_ctx, sizeof(SHA512_CTX));
	SHA512_Update(&ctx512, (u_int8_t*)&sector, sizeof(off_t));
	SHA512_Final(md, &ctx512);

	memcpy(iv, md, iv_len);
	dmtc_crypto_dispatch(opaque);
}
#endif

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

static int
dm_target_crypt_init(dm_dev_t * dmv, void **target_config, char *params)
{
	dm_target_crypt_config_t *priv;
	size_t len;
	char **ap, *args[5];
	char *crypto_alg, *crypto_mode, *iv_mode, *iv_opt, *key, *dev;
	char *status_str;
	int i, argc, klen, error;
	uint64_t iv_offset, block_offset;

	if (params == NULL)
		return EINVAL;

	len = strlen(params) + 1;
	argc = 0;

	status_str = kmalloc(len, M_DMCRYPT, M_WAITOK);
	/*
	 * Parse a string, containing tokens delimited by white space,
	 * into an argument vector
	 */
	for (ap = args; ap < &args[5] &&
	    (*ap = strsep(&params, " \t")) != NULL;) {
		if (**ap != '\0') {
			argc++;
			ap++;
		}
	}

	if (argc != 5) {
		kprintf("dm_target_crypt: not enough arguments, "
			"need exactly 5\n");
		kfree(status_str, M_DMCRYPT);
		return ENOMEM; /* XXX */
	}

	crypto_alg = strsep(&args[0], "-");
	crypto_mode = strsep(&args[0], "-");
	iv_opt = strsep(&args[0], "-");
	iv_mode = strsep(&iv_opt, ":");
	key = args[1];
	iv_offset = strtouq(args[2], NULL, 0);
	dev = args[3];
	block_offset = strtouq(args[4], NULL, 0);
	/* bits / 8 = bytes, 1 byte = 2 hexa chars, so << 2 */
	klen = strlen(key) << 2;

#if 0
	kprintf("dm_target_crypt - new: dev=%s, crypto_alg=%s, crypto_mode=%s, "
		"iv_mode=%s, iv_opt=%s, key=%s, iv_offset=%ju, "
		"block_offset=%ju\n",
		dev, crypto_alg, crypto_mode, iv_mode, iv_opt, key, iv_offset,
		block_offset);
#endif

	priv = kmalloc(sizeof(dm_target_crypt_config_t), M_DMCRYPT, M_WAITOK);

	/* Insert dmp to global pdev list */
	if ((priv->pdev = dm_pdev_insert(dev)) == NULL) {
		kprintf("dm_target_crypt: dm_pdev_insert failed\n");
		kfree(status_str, M_DMCRYPT);
		return ENOENT;
	}

	/*
	 * This code checks for valid combinations of algorithm and mode.
	 * Currently supported options are:
	 *
	 * *-cbc
	 * aes-xts
	 * twofish-xts
	 * serpent-xts
	 */
	if ((strcmp(crypto_mode, "cbc") != 0) &&
	    !((strcmp(crypto_mode, "xts") == 0) &&
	    ((strcmp(crypto_alg, "aes") == 0) ||
	    (strcmp(crypto_alg, "twofish") == 0) ||
	    (strcmp(crypto_alg, "serpent") == 0))))
	{
		kprintf("dm_target_crypt: only support 'cbc' chaining mode,"
		    " aes-xts, twofish-xts and serpent-xts, "
		    "invalid mode '%s-%s'\n",
		    crypto_alg, crypto_mode);
		goto notsup;
	}

	if (!strcmp(crypto_alg, "aes")) {
		if (!strcmp(crypto_mode, "xts")) {
			priv->crypto_alg = CRYPTO_AES_XTS;
			if (klen != 256 && klen != 512)
				goto notsup;
		} else if (!strcmp(crypto_mode, "cbc")) {
			priv->crypto_alg = CRYPTO_AES_CBC;
			if (klen != 128 && klen != 192 && klen != 256)
				goto notsup;
		} else {
			goto notsup;
		}
		priv->crypto_klen = klen;
	} else if (!strcmp(crypto_alg, "twofish")) {
		if (!strcmp(crypto_mode, "xts")) {
			priv->crypto_alg = CRYPTO_TWOFISH_XTS;
			if (klen != 256 && klen != 512)
				goto notsup;
		} else if (!strcmp(crypto_mode, "cbc")) {
			priv->crypto_alg = CRYPTO_TWOFISH_CBC;
			if (klen != 128 && klen != 192 && klen != 256)
				goto notsup;
		} else {
			goto notsup;
		}
		priv->crypto_klen = klen;
	} else if (!strcmp(crypto_alg, "serpent")) {
		if (!strcmp(crypto_mode, "xts")) {
			priv->crypto_alg = CRYPTO_SERPENT_XTS;
			if (klen != 256 && klen != 512)
				goto notsup;
		} else if (!strcmp(crypto_mode, "cbc")) {
			priv->crypto_alg = CRYPTO_SERPENT_CBC;
			if (klen != 128 && klen != 192 && klen != 256)
				goto notsup;
		} else {
			goto notsup;
		}
		priv->crypto_klen = klen;
	} else if (!strcmp(crypto_alg, "blowfish")) {
		priv->crypto_alg = CRYPTO_BLF_CBC;
		if (klen < 128 || klen > 448 || (klen % 8) != 0)
			goto notsup;
		priv->crypto_klen = klen;
	} else if (!strcmp(crypto_alg, "3des") ||
		   !strncmp(crypto_alg, "des3", 4)) {
		priv->crypto_alg = CRYPTO_3DES_CBC;
		if (klen != 168)
			goto notsup;
		priv->crypto_klen = 168;
	} else if (!strcmp(crypto_alg, "camellia")) {
		priv->crypto_alg = CRYPTO_CAMELLIA_CBC;
		if (klen != 128 && klen != 192 && klen != 256)
			goto notsup;
		priv->crypto_klen = klen;
	} else if (!strcmp(crypto_alg, "skipjack")) {
		priv->crypto_alg = CRYPTO_SKIPJACK_CBC;
		if (klen != 80)
			goto notsup;
		priv->crypto_klen = 80;
	} else if (!strcmp(crypto_alg, "cast5")) {
		priv->crypto_alg = CRYPTO_CAST_CBC;
		if (klen != 128)
			goto notsup;
		priv->crypto_klen = 128;
	} else if (!strcmp(crypto_alg, "null")) {
		priv->crypto_alg = CRYPTO_NULL_CBC;
		if (klen != 128)
			goto notsup;
		priv->crypto_klen = 128;
	} else {
		kprintf("dm_target_crypt: Unsupported crypto algorithm: %s\n",
			crypto_alg);
		goto notsup;
	}

	/* Save length of param string */
	priv->params_len = len;
	priv->block_offset = block_offset;
	priv->iv_offset = iv_offset - block_offset;

	*target_config = priv;

	dmv->dev_type = DM_CRYPTO_DEV;

	error = hex2key(key, priv->crypto_klen >> 3,
			(u_int8_t *)priv->crypto_key);

	if (error) {
		kprintf("dm_target_crypt: hex2key failed, "
			"invalid key format\n");
		goto notsup;
	}

	/* Handle cmd */
	for(i = 0; ivgens[i].name != NULL; i++) {
		if (!strcmp(iv_mode, ivgens[i].name))
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

	priv->crypto_session.cri_alg = priv->crypto_alg;
	priv->crypto_session.cri_key = (u_int8_t *)priv->crypto_key;
	priv->crypto_session.cri_klen = priv->crypto_klen;
	priv->crypto_session.cri_mlen = 0;
	priv->crypto_session.cri_next = NULL;

	error = crypto_newsession(&priv->crypto_sid,
				  &priv->crypto_session,
				  CRYPTOCAP_F_SOFTWARE | CRYPTOCAP_F_HARDWARE);
	if (error) {
		kprintf("dm_target_crypt: Error during crypto_newsession, "
			"error = %d\n",
			error);
		goto notsup;
	}

	memset(key, '0', strlen(key));
	if (iv_opt) {
		ksprintf(status_str, "%s-%s-%s:%s %s %ju %s %ju",
		    crypto_alg, crypto_mode, iv_mode, iv_opt,
		    key, iv_offset, dev, block_offset);
	} else {
		ksprintf(status_str, "%s-%s-%s %s %ju %s %ju",
		    crypto_alg, crypto_mode, iv_mode,
		    key, iv_offset, dev, block_offset);
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

/* Status routine called to get params string. */
static char *
dm_target_crypt_status(void *target_config)
{
	dm_target_crypt_config_t *priv;
	char *params;

	priv = target_config;

	/* caller expects use of M_DM */
	params = kmalloc(DM_MAX_PARAMS_SIZE, M_DM, M_WAITOK);

	ksnprintf(params, DM_MAX_PARAMS_SIZE, "%s",
	    priv->status_str);

	return params;
}

static int
dm_target_crypt_destroy(dm_table_entry_t * table_en)
{
	dm_target_crypt_config_t *priv;

	/*
	 * Disconnect the crypt config before unbusying the target.
	 */
	priv = table_en->target_config;
	if (priv == NULL)
		return 0;
	table_en->target_config = NULL;
	dm_pdev_decr(priv->pdev);

	/*
	 * Clean up the crypt config
	 *
	 * Overwrite the private information before freeing memory to
	 * avoid leaking it.
	 */
	if (priv->status_str) {
		dmtc_crypto_clear(priv->status_str, strlen(priv->status_str));
		kfree(priv->status_str, M_DMCRYPT);
		crypto_freesession(priv->crypto_sid);
	}

	if ((priv->ivgen) && (priv->ivgen->dtor != NULL)) {
		priv->ivgen->dtor(priv, priv->ivgen_priv);
	}

	/* Destroy mpipes */
	dmtc_destroy_mpipe(priv);

	dmtc_crypto_clear(priv, sizeof(dm_target_crypt_config_t));
	kfree(priv, M_DMCRYPT);

	return 0;
}

static int
dm_target_crypt_deps(dm_table_entry_t * table_en, prop_array_t prop_array)
{
	dm_target_crypt_config_t *priv;
	struct vattr va;

	int error;

	if (table_en->target_config == NULL)
		return ENOENT;

	priv = table_en->target_config;

	if ((error = VOP_GETATTR(priv->pdev->pdev_vnode, &va)) != 0)
		return error;

	prop_array_add_uint64(prop_array,
			      (uint64_t)makeudev(major(va.va_rdev), minor(va.va_rdev)));

	return 0;
}

/* Unsupported for this target. */
static int
dm_target_crypt_upcall(dm_table_entry_t * table_en, struct buf * bp)
{
	return 0;
}

/************************************************************************
 *			STRATEGY SUPPORT FUNCTIONS			*
 ************************************************************************
 *
 * READ PATH:	doio -> bio_read_done -> crypto_work -> crypto_cb_read_done
 * WRITE PATH:	crypto_work -> crypto_cb_write_done -> doio -> bio_write_done
 */

/*
 * Wrapper around crypto_dispatch() to match dispatch_t type
 */
static void
dmtc_crypto_dispatch(void *arg)
{
	struct cryptop *crp;

	crp = (struct cryptop *)arg;
	KKASSERT(crp != NULL);
	KTR_LOG(dmcrypt_crypto_dispatch, crp);
	crypto_dispatch(crp);
}

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
	if ((bp->b_cmd == BUF_CMD_READ) || (bp->b_cmd == BUF_CMD_WRITE)) {
		if (((bp->b_bcount % DEV_BSIZE) != 0) || (bp->b_bcount == 0)) {
			kprintf("dm_target_crypt_strategy: can't really "
				"handle bp->b_bcount = %d\n",
				bp->b_bcount);
			bp->b_error = EINVAL;
			bp->b_flags |= B_ERROR | B_INVAL;
			biodone(&bp->b_bio1);
			return 0;
		}
	}

	KTR_LOG(dmcrypt_crypt_strategy, bp->b_cmd, bp);

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
		dmtc_crypto_write_start(priv, bio);
		break;
	default:
		vn_strategy(priv->pdev->pdev_vnode, &bp->b_bio1);
		break;
	}
	return 0;
}

/*
 * STRATEGY READ PATH PART 1/3 (after read BIO completes)
 */
static void
dmtc_bio_read_done(struct bio *bio)
{
	struct bio *obio;

	dm_target_crypt_config_t *priv;

	KTR_LOG(dmcrypt_bio_read_done, bio->bio_buf);

	/*
	 * If a read error occurs we shortcut the operation, otherwise
	 * go on to stage 2.
	 */
	if (bio->bio_buf->b_flags & B_ERROR) {
		obio = pop_bio(bio);
		biodone(obio);
	} else {
		priv = bio->bio_caller_info1.ptr;
		dmtc_crypto_read_start(priv, bio);
	}
}

/*
 * STRATEGY READ PATH PART 2/3
 */
static void
dmtc_crypto_read_retry(void *arg1, void *arg2)
{
	dm_target_crypt_config_t *priv = arg1;
	struct bio *bio = arg2;

	dmtc_crypto_read_start(priv, bio);
}

static void
dmtc_crypto_read_start(dm_target_crypt_config_t *priv, struct bio *bio)
{
	struct dmtc_helper *dmtc;
	struct cryptodesc *crd;
	struct cryptop *crp;
	int i, bytes, sectors, sz;
	off_t isector;
	u_char *ptr, *space;

	/*
	 * Note: b_resid no good after read I/O, it will be 0, use
	 *	 b_bcount.
	 */
	bytes = bio->bio_buf->b_bcount;
	isector = bio->bio_offset / DEV_BSIZE;	/* ivgen salt base? */
	sectors = bytes / DEV_BSIZE;		/* Number of sectors */
	sz = sectors * (sizeof(*crp) + sizeof(*crd));

	/*
	 * For reads with bogus page we can't decrypt in place as stuff
	 * can get ripped out from under us.
	 *
	 * XXX actually it looks like we can, and in any case the initial
	 * read already completed and threw crypted data into the buffer
	 * cache buffer.  Disable for now.
	 */
	space = mpipe_alloc_callback(&priv->read_mpipe,
				     dmtc_crypto_read_retry, priv, bio);
	if (space == NULL)
		return;

	dmtc = (struct dmtc_helper *)space;
	dmtc->free_addr = space;
	space += sizeof(struct dmtc_helper);
	dmtc->orig_buf = NULL;
	dmtc->data_buf = bio->bio_buf->b_data;
	dmtc->priv = priv;
	bio->bio_caller_info2.ptr = dmtc;
	bio->bio_buf->b_error = 0;

	/*
	 * Load crypto descriptors (crp/crd loop)
	 */
	bzero(space, sz);
	ptr = space;
	bio->bio_caller_info3.value = sectors;
	cpu_sfence();
#if 0
	kprintf("Read, bytes = %d (b_bcount), "
		"sectors = %d (bio = %p, b_cmd = %d)\n",
		bytes, sectors, bio, bio->bio_buf->b_cmd);
#endif
	for (i = 0; i < sectors; i++) {
		crp = (struct cryptop *)ptr;
		ptr += sizeof(*crp);
		crd = (struct cryptodesc *)ptr;
		ptr += sizeof (*crd);

		crp->crp_buf = dmtc->data_buf + i * DEV_BSIZE;

		crp->crp_sid = priv->crypto_sid;
		crp->crp_ilen = crp->crp_olen = DEV_BSIZE;

		crp->crp_opaque = (void *)bio;

		crp->crp_callback = dmtc_crypto_cb_read_done;
		crp->crp_desc = crd;
		crp->crp_etype = 0;
		crp->crp_flags = CRYPTO_F_CBIFSYNC | CRYPTO_F_REL |
				 CRYPTO_F_BATCH;

		crd->crd_alg = priv->crypto_alg;
#if 0
		crd->crd_key = (caddr_t)priv->crypto_key;
		crd->crd_klen = priv->crypto_klen;
#endif

		crd->crd_skip = 0;
		crd->crd_len = DEV_BSIZE /* XXX */;
		crd->crd_flags = CRD_F_IV_EXPLICIT | CRD_F_IV_PRESENT;
		crd->crd_next = NULL;

		crd->crd_flags &= ~CRD_F_ENCRYPT;

		KTR_LOG(dmcrypt_crypto_read_start, crp, bio->bio_buf, i,
		    sectors);

		/*
		 * Note: last argument is used to generate salt(?) and is
		 *	 a 64 bit value, but the original code passed an
		 *	 int.  Changing it now will break pre-existing
		 *	 crypt volumes.
		 */
		priv->ivgen->gen_iv(priv, crd->crd_iv, sizeof(crd->crd_iv),
				    isector + i, crp);
	}
}

/*
 * STRATEGY READ PATH PART 3/3
 */
static int
dmtc_crypto_cb_read_done(struct cryptop *crp)
{
	struct dmtc_helper *dmtc;
	struct bio *bio, *obio;
	int n;

	if (crp->crp_etype == EAGAIN)
		return crypto_dispatch(crp);

	bio = (struct bio *)crp->crp_opaque;
	KKASSERT(bio != NULL);

	/*
	 * Cumulative error
	 */
	if (crp->crp_etype) {
		kprintf("dm_target_crypt: dmtc_crypto_cb_read_done "
			"crp_etype = %d\n",
			crp->crp_etype);
		bio->bio_buf->b_error = crp->crp_etype;
	}

	/*
	 * On the last chunk of the decryption we do any required copybacks
	 * and complete the I/O.
	 */
	n = atomic_fetchadd_int(&bio->bio_caller_info3.value, -1);
#if 0
	kprintf("dmtc_crypto_cb_read_done %p, n = %d\n", bio, n);
#endif

	KTR_LOG(dmcrypt_crypto_cb_read_done, crp, bio->bio_buf, n);

	if (n == 1) {
		/*
		 * For the B_HASBOGUS case we didn't decrypt in place,
		 * so we need to copy stuff back into the buf.
		 *
		 * (disabled for now).
		 */
		dmtc = bio->bio_caller_info2.ptr;
		if (bio->bio_buf->b_error) {
			bio->bio_buf->b_flags |= B_ERROR;
		}
#if 0
		else if (bio->bio_buf->b_flags & B_HASBOGUS) {
			memcpy(bio->bio_buf->b_data, dmtc->data_buf,
			       bio->bio_buf->b_bcount);
		}
#endif
		mpipe_free(&dmtc->priv->read_mpipe, dmtc->free_addr);
		obio = pop_bio(bio);
		biodone(obio);
	}
	return 0;
}
/* END OF STRATEGY READ SECTION */

/*
 * STRATEGY WRITE PATH PART 1/3
 */

static void
dmtc_crypto_write_retry(void *arg1, void *arg2)
{
	dm_target_crypt_config_t *priv = arg1;
	struct bio *bio = arg2;

	KTR_LOG(dmcrypt_crypto_write_retry, bio->bio_buf);

	dmtc_crypto_write_start(priv, bio);
}

static void
dmtc_crypto_write_start(dm_target_crypt_config_t *priv, struct bio *bio)
{
	struct dmtc_helper *dmtc;
	struct cryptodesc *crd;
	struct cryptop *crp;
	int i, bytes, sectors, sz;
	off_t isector;
	u_char *ptr, *space;

	/*
	 * Use b_bcount for consistency
	 */
	bytes = bio->bio_buf->b_bcount;

	isector = bio->bio_offset / DEV_BSIZE;	/* ivgen salt base? */
	sectors = bytes / DEV_BSIZE;		/* Number of sectors */
	sz = sectors * (sizeof(*crp) + sizeof(*crd));

	/*
	 * For writes and reads with bogus page don't decrypt in place.
	 */
	space = mpipe_alloc_callback(&priv->write_mpipe,
				     dmtc_crypto_write_retry, priv, bio);
	if (space == NULL)
		return;

	dmtc = (struct dmtc_helper *)space;
	dmtc->free_addr = space;
	space += sizeof(struct dmtc_helper);
	memcpy(space + sz, bio->bio_buf->b_data, bytes);

	bio->bio_caller_info2.ptr = dmtc;
	bio->bio_buf->b_error = 0;

	dmtc->orig_buf = bio->bio_buf->b_data;
	dmtc->data_buf = space + sz;
	dmtc->priv = priv;

	/*
	 * Load crypto descriptors (crp/crd loop)
	 */
	bzero(space, sz);
	ptr = space;
	bio->bio_caller_info3.value = sectors;
	cpu_sfence();
#if 0
	kprintf("Write, bytes = %d (b_bcount), "
		"sectors = %d (bio = %p, b_cmd = %d)\n",
		bytes, sectors, bio, bio->bio_buf->b_cmd);
#endif
	for (i = 0; i < sectors; i++) {
		crp = (struct cryptop *)ptr;
		ptr += sizeof(*crp);
		crd = (struct cryptodesc *)ptr;
		ptr += sizeof (*crd);

		crp->crp_buf = dmtc->data_buf + i * DEV_BSIZE;

		crp->crp_sid = priv->crypto_sid;
		crp->crp_ilen = crp->crp_olen = DEV_BSIZE;

		crp->crp_opaque = (void *)bio;

		crp->crp_callback = dmtc_crypto_cb_write_done;
		crp->crp_desc = crd;
		crp->crp_etype = 0;
		crp->crp_flags = CRYPTO_F_CBIFSYNC | CRYPTO_F_REL |
				 CRYPTO_F_BATCH;

		crd->crd_alg = priv->crypto_alg;
#if 0
		crd->crd_key = (caddr_t)priv->crypto_key;
		crd->crd_klen = priv->crypto_klen;
#endif

		crd->crd_skip = 0;
		crd->crd_len = DEV_BSIZE /* XXX */;
		crd->crd_flags = CRD_F_IV_EXPLICIT | CRD_F_IV_PRESENT;
		crd->crd_next = NULL;

		crd->crd_flags |= CRD_F_ENCRYPT;

		/*
		 * Note: last argument is used to generate salt(?) and is
		 *	 a 64 bit value, but the original code passed an
		 *	 int.  Changing it now will break pre-existing
		 *	 crypt volumes.
		 */

		KTR_LOG(dmcrypt_crypto_write_start, crp, bio->bio_buf,
		    i, sectors);

		priv->ivgen->gen_iv(priv, crd->crd_iv, sizeof(crd->crd_iv),
				    isector + i, crp);
	}
}

/*
 * STRATEGY WRITE PATH PART 2/3
 */
static int
dmtc_crypto_cb_write_done(struct cryptop *crp)
{
	struct dmtc_helper *dmtc;
	dm_target_crypt_config_t *priv;
	struct bio *bio, *obio;
	int n;

	if (crp->crp_etype == EAGAIN)
		return crypto_dispatch(crp);

	bio = (struct bio *)crp->crp_opaque;
	KKASSERT(bio != NULL);

	/*
	 * Cumulative error
	 */
	if (crp->crp_etype != 0) {
		kprintf("dm_target_crypt: dmtc_crypto_cb_write_done "
			"crp_etype = %d\n",
		crp->crp_etype);
		bio->bio_buf->b_error = crp->crp_etype;
	}

	/*
	 * On the last chunk of the encryption we issue the write
	 */
	n = atomic_fetchadd_int(&bio->bio_caller_info3.value, -1);
#if 0
	kprintf("dmtc_crypto_cb_write_done %p, n = %d\n", bio, n);
#endif

	KTR_LOG(dmcrypt_crypto_cb_write_done, crp, bio->bio_buf, n);

	if (n == 1) {
		dmtc = bio->bio_caller_info2.ptr;
		priv = (dm_target_crypt_config_t *)bio->bio_caller_info1.ptr;

		if (bio->bio_buf->b_error) {
			bio->bio_buf->b_flags |= B_ERROR;
			mpipe_free(&dmtc->priv->write_mpipe, dmtc->free_addr);
			obio = pop_bio(bio);
			biodone(obio);
		} else {
			dmtc->orig_buf = bio->bio_buf->b_data;
			bio->bio_buf->b_data = dmtc->data_buf;
			bio->bio_done = dmtc_bio_write_done;
			vn_strategy(priv->pdev->pdev_vnode, bio);
		}
	}
	return 0;
}

/*
 * STRATEGY WRITE PATH PART 3/3
 */
static void
dmtc_bio_write_done(struct bio *bio)
{
	struct dmtc_helper *dmtc;
	struct bio *obio;

	dmtc = bio->bio_caller_info2.ptr;
	bio->bio_buf->b_data = dmtc->orig_buf;
	mpipe_free(&dmtc->priv->write_mpipe, dmtc->free_addr);

	KTR_LOG(dmcrypt_bio_write_done, bio->bio_buf);

	obio = pop_bio(bio);
	biodone(obio);
}
/* END OF STRATEGY WRITE SECTION */



/* DUMPING MAGIC */

extern int tsleep_crypto_dump;

static int
dm_target_crypt_dump(dm_table_entry_t *table_en, void *data, size_t length, off_t offset)
{
	static struct dmtc_dump_helper dump_helper;
	dm_target_crypt_config_t *priv;
	int id;
	static int first_call = 1;

	priv = table_en->target_config;

	if (first_call) {
		first_call = 0;
		dump_reactivate_cpus();
	}

	/* Magically enable tsleep */
	tsleep_crypto_dump = 1;
	id = 0;

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
	dump_helper.ident = &id;
	dmtc_crypto_dump_start(priv, &dump_helper);

	/*
	 * Hackery to make stuff appear synchronous. The crypto callback will
	 * set id to 1 and call wakeup on it. If the request completed
	 * synchronously, id will be 1 and we won't bother to sleep. If not,
	 * the crypto request will complete asynchronously and we sleep until
	 * it's done.
	 */
	if (id == 0)
		tsleep(&dump_helper, 0, "cryptdump", 0);

	dump_helper.offset = dm_pdev_correct_dump_offset(priv->pdev,
	    dump_helper.offset);

	dev_ddump(priv->pdev->pdev_vnode->v_rdev,
	    dump_helper.space, 0, dump_helper.offset,
	    dump_helper.length);

	tsleep_crypto_dump = 0;
	return 0;
}

static void
dmtc_crypto_dump_start(dm_target_crypt_config_t *priv, struct dmtc_dump_helper *dump_helper)
{
	struct cryptodesc *crd;
	struct cryptop *crp;
	int i, bytes, sectors;
	off_t isector;

	bytes = dump_helper->length;

	isector = dump_helper->offset / DEV_BSIZE;	/* ivgen salt base? */
	sectors = bytes / DEV_BSIZE;		/* Number of sectors */
	dump_helper->sectors = sectors;
#if 0
	kprintf("Dump, bytes = %d, "
		"sectors = %d, LENGTH=%zu\n", bytes, sectors, dump_helper->length);
#endif
	KKASSERT(dump_helper->length <= 65536);

	memcpy(dump_helper->space, dump_helper->data, bytes);

	cpu_sfence();

	for (i = 0; i < sectors; i++) {
		crp = &dump_helper->crp[i];
		crd = &dump_helper->crd[i];

		crp->crp_buf = dump_helper->space + i * DEV_BSIZE;

		crp->crp_sid = priv->crypto_sid;
		crp->crp_ilen = crp->crp_olen = DEV_BSIZE;

		crp->crp_opaque = (void *)dump_helper;

		crp->crp_callback = dmtc_crypto_cb_dump_done;
		crp->crp_desc = crd;
		crp->crp_etype = 0;
		crp->crp_flags = CRYPTO_F_CBIFSYNC | CRYPTO_F_REL |
				 CRYPTO_F_BATCH;

		crd->crd_alg = priv->crypto_alg;

		crd->crd_skip = 0;
		crd->crd_len = DEV_BSIZE /* XXX */;
		crd->crd_flags = CRD_F_IV_EXPLICIT | CRD_F_IV_PRESENT;
		crd->crd_next = NULL;

		crd->crd_flags |= CRD_F_ENCRYPT;

		/*
		 * Note: last argument is used to generate salt(?) and is
		 *	 a 64 bit value, but the original code passed an
		 *	 int.  Changing it now will break pre-existing
		 *	 crypt volumes.
		 */
		priv->ivgen->gen_iv(priv, crd->crd_iv, sizeof(crd->crd_iv),
				    isector + i, crp);
	}
}

static int
dmtc_crypto_cb_dump_done(struct cryptop *crp)
{
	struct dmtc_dump_helper *dump_helper;
	int n;

	if (crp->crp_etype == EAGAIN)
		return crypto_dispatch(crp);

	dump_helper = (struct dmtc_dump_helper *)crp->crp_opaque;
	KKASSERT(dump_helper != NULL);

	if (crp->crp_etype != 0) {
		kprintf("dm_target_crypt: dmtc_crypto_cb_dump_done "
			"crp_etype = %d\n",
		crp->crp_etype);
		return crp->crp_etype;
	}

	/*
	 * On the last chunk of the encryption we return control
	 */
	n = atomic_fetchadd_int(&dump_helper->sectors, -1);

	if (n == 1) {
		atomic_add_int(dump_helper->ident, 1);
		wakeup(dump_helper);
	}

	return 0;
}

static int
dmtc_mod_handler(module_t mod, int type, void *unused)
{
	dm_target_t *dmt = NULL;
	int err = 0;

	switch (type) {
	case MOD_LOAD:
		if ((dmt = dm_target_lookup("crypt")) != NULL) {
			dm_target_unbusy(dmt);
			return EEXIST;
		}
		dmt = dm_target_alloc("crypt");
		dmt->version[0] = 1;
		dmt->version[1] = 6;
		dmt->version[2] = 0;
		strlcpy(dmt->name, "crypt", DM_MAX_TYPE_NAME);
		dmt->init = &dm_target_crypt_init;
		dmt->status = &dm_target_crypt_status;
		dmt->strategy = &dm_target_crypt_strategy;
		dmt->deps = &dm_target_crypt_deps;
		dmt->destroy = &dm_target_crypt_destroy;
		dmt->upcall = &dm_target_crypt_upcall;
		dmt->dump = &dm_target_crypt_dump;

		err = dm_target_insert(dmt);
		if (!err)
			kprintf("dm_target_crypt: Successfully initialized\n");
		break;

	case MOD_UNLOAD:
		err = dm_target_rem("crypt");
		if (err == 0) {
			kprintf("dm_target_crypt: unloaded\n");
		}
		break;

	default:
		break;
	}

	return err;
}

DM_TARGET_MODULE(dm_target_crypt, dmtc_mod_handler);
