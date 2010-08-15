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
#include <sys/malloc.h>
#include <sys/md5.h>
#include <sys/vnode.h>
#include <crypto/sha1.h>
#include <crypto/sha2/sha2.h>
#include <opencrypto/cryptodev.h>
#include <opencrypto/rmd160.h>
#include <machine/cpufunc.h>

#include "dm.h"
MALLOC_DEFINE(M_DMCRYPT, "dm_crypt", "Device Mapper Target Crypt");

struct target_crypt_config;
typedef void ivgen_t(struct target_crypt_config *, u_int8_t *, size_t, off_t);

typedef struct target_crypt_config {
	size_t	params_len;
	dm_pdev_t *pdev;
	char	*status_str;
	int	crypto_alg;
	int	crypto_klen;
	u_int8_t	crypto_key[512>>3];
	u_int8_t	crypto_keyhash[SHA512_DIGEST_LENGTH];
	u_int64_t	crypto_sid;
	u_int64_t	block_offset;
	u_int64_t	iv_offset;
	SHA512_CTX	essivsha512_ctx;
	struct cryptoini	crypto_session;
	ivgen_t	*crypto_ivgen;
} dm_target_crypt_config_t;

struct dmtc_helper {
	caddr_t	free_addr;
	caddr_t	orig_buf;
	caddr_t data_buf;
};

static void dmtc_crypto_read_start(dm_target_crypt_config_t *priv,
				struct bio *bio);
static void dmtc_crypto_write_start(dm_target_crypt_config_t *priv,
				struct bio *bio);
static void dmtc_bio_read_done(struct bio *bio);
static void dmtc_bio_write_done(struct bio *bio);
static int dmtc_crypto_cb_read_done(struct cryptop *crp);
static int dmtc_crypto_cb_write_done(struct cryptop *crp);

/*
 * Support routines for dm_target_crypt_init
 */
static int
essiv_hash_mkey(dm_target_crypt_config_t *priv, char *iv_hash)
{
	unsigned int klen;

	klen = (priv->crypto_klen >> 3);

	if (iv_hash == NULL)
		return EINVAL;

	if (!strcmp(iv_hash, "sha1")) {
		SHA1_CTX ctx;

		if (klen != SHA1_RESULTLEN)
			return EINVAL;

		SHA1Init(&ctx);
		SHA1Update(&ctx, priv->crypto_key, priv->crypto_klen>>3);
		SHA1Final(priv->crypto_keyhash, &ctx);
	} else if (!strcmp(iv_hash, "sha256")) {
		SHA256_CTX ctx;

		if (klen != SHA256_DIGEST_LENGTH)
			return EINVAL;

		SHA256_Init(&ctx);
		SHA256_Update(&ctx, priv->crypto_key, priv->crypto_klen>>3);
		SHA256_Final(priv->crypto_keyhash, &ctx);
	} else if (!strcmp(iv_hash, "sha384")) {
		SHA384_CTX ctx;

		if (klen != SHA384_DIGEST_LENGTH)
			return EINVAL;

		SHA384_Init(&ctx);
		SHA384_Update(&ctx, priv->crypto_key, priv->crypto_klen>>3);
		SHA384_Final(priv->crypto_keyhash, &ctx);
	} else if (!strcmp(iv_hash, "sha512")) {
		SHA512_CTX ctx;

		if (klen != SHA512_DIGEST_LENGTH)
			return EINVAL;

		SHA512_Init(&ctx);
		SHA512_Update(&ctx, priv->crypto_key, priv->crypto_klen>>3);
		SHA512_Final(priv->crypto_keyhash, &ctx);
	} else if (!strcmp(iv_hash, "md5")) {
		MD5_CTX ctx;

		if (klen != MD5_DIGEST_LENGTH)
			return EINVAL;

		MD5Init(&ctx);
		MD5Update(&ctx, priv->crypto_key, priv->crypto_klen>>3);
		MD5Final(priv->crypto_keyhash, &ctx);
	} else if (!strcmp(iv_hash, "rmd160") ||
		   !strcmp(iv_hash, "ripemd160")) {
		RMD160_CTX ctx;

		if (klen != (160/8))
			return EINVAL;

		RMD160Init(&ctx);
		RMD160Update(&ctx, priv->crypto_key, priv->crypto_klen>>3);
		RMD160Final(priv->crypto_keyhash, &ctx);
	} else {
		return EINVAL;
	}

	return 0;
}

static int
essiv_ivgen_done(struct cryptop *crp)
{

	if (crp->crp_etype == EAGAIN)
		return crypto_dispatch(crp);

	if (crp->crp_etype != 0) {
		kprintf("dm_target_crypt: essiv_ivgen_done, "
			"crp->crp_etype = %d\n", crp->crp_etype);
	}

	atomic_add_int((int *)crp->crp_opaque, 1);
	wakeup(crp->crp_opaque);
	return 0;
}

static void
plain_ivgen(dm_target_crypt_config_t *priv, u_int8_t *iv,
	    size_t iv_len, off_t sector)
{
	bzero(iv, iv_len);
	*((off_t *)iv) = htole64(sector + priv->iv_offset);
}

static void
essiv_ivgen(dm_target_crypt_config_t *priv, u_int8_t *iv,
	    size_t iv_len, off_t sector)
{
	struct cryptodesc crd;
	struct cryptop crp;
	int error, id;

	id = 0;
	bzero(iv, iv_len);
	bzero(&crd, sizeof(crd));
	bzero(&crp, sizeof(crp));
	*((off_t *)iv) = htole64(sector + priv->iv_offset);
	crp.crp_buf = (caddr_t)iv;

	crp.crp_sid = priv->crypto_sid;
	crp.crp_ilen = crp.crp_olen = iv_len;

	crp.crp_opaque = (void *)&id;

	crp.crp_callback = essiv_ivgen_done;

	crp.crp_desc = &crd;
	crp.crp_etype = 0;
	crp.crp_flags = CRYPTO_F_CBIFSYNC | CRYPTO_F_REL;

	crd.crd_alg = priv->crypto_alg;
	crd.crd_key = (caddr_t)priv->crypto_keyhash;
	crd.crd_klen = priv->crypto_klen;

	bzero(crd.crd_iv, sizeof(crd.crd_iv));

	crd.crd_skip = 0;
	crd.crd_len = iv_len;
	crd.crd_flags = CRD_F_KEY_EXPLICIT | CRD_F_IV_EXPLICIT |
			CRD_F_IV_PRESENT;
	crd.crd_flags |= CRD_F_ENCRYPT;
	crd.crd_next = NULL;

	error = crypto_dispatch(&crp);
	if (error)
		kprintf("dm_target_crypt: essiv_ivgen, error = %d\n", error);

	/*
	 * id is modified in the callback, so that if crypto_dispatch finishes
	 * synchronously we don't tsleep() forever.
	 */
	if (id == 0)
		tsleep((void *)&error, 0, "essivgen", 0);
}

#if 0

static void
geli_ivgen(dm_target_crypt_config_t *priv, u_int8_t *iv,
	   size_t iv_len, off_t sector)
{

	SHA512_CTX	ctx512;
	u_int8_t	md[SHA512_DIGEST_LENGTH]; /* Max. Digest Size */

	memcpy(&ctx512, &priv->essivsha512_ctx, sizeof(SHA512_CTX));
	SHA512_Update(&ctx512, (u_int8_t*)&sector, sizeof(off_t));
	SHA512_Final(md, &ctx512);

	memcpy(iv, md, iv_len);
}

#endif

#ifdef DM_TARGET_MODULE
/*
 * Every target can be compiled directly to dm driver or as a
 * separate module this part of target is used for loading targets
 * to dm driver.
 * Target can be unloaded from kernel only if there are no users of
 * it e.g. there are no devices which uses that target.
 */
#include <sys/kernel.h>
#include <sys/module.h>

static int
dm_target_crypt_modcmd(modcmd_t cmd, void *arg)
{
	dm_target_t *dmt;
	int r;
	dmt = NULL;

	switch (cmd) {
	case MODULE_CMD_INIT:
		if ((dmt = dm_target_lookup("crypt")) != NULL) {
			dm_target_unbusy(dmt);
			return EEXIST;
		}
		dmt = dm_target_alloc("crypt");

		dmt->version[0] = 1;
		dmt->version[1] = 0;
		dmt->version[2] = 0;
		strlcpy(dmt->name, "crypt", DM_MAX_TYPE_NAME);
		dmt->init = &dm_target_crypt_init;
		dmt->status = &dm_target_crypt_status;
		dmt->strategy = &dm_target_crypt_strategy;
		dmt->deps = &dm_target_crypt_deps;
		dmt->destroy = &dm_target_crypt_destroy;
		dmt->upcall = &dm_target_crypt_upcall;

		r = dm_target_insert(dmt);

		break;

	case MODULE_CMD_FINI:
		r = dm_target_rem("crypt");
		break;

	case MODULE_CMD_STAT:
		return ENOTTY;

	default:
		return ENOTTY;
	}

	return r;
}

#endif

/*
 * Init function called from dm_table_load_ioctl.
 * cryptsetup actually passes us this:
 * aes-cbc-essiv:sha256 7997f8af... 0 /dev/ad0s0a 8
 */
static int
hex2key(char *hex, size_t hex_length, u_int8_t *key)
{
	char hex_buf[3];
	size_t key_idx;

	key_idx = 0;
	bzero(hex_buf, sizeof(hex_buf));

	for (; hex_length > 0; hex_length -= 2) {
		hex_buf[0] = *hex++;
		hex_buf[1] = *hex++;
		key[key_idx++] = (u_int8_t)strtoul(hex_buf, NULL, 16);
	}

	return 0;
}

int
dm_target_crypt_init(dm_dev_t * dmv, void **target_config, char *params)
{
	dm_target_crypt_config_t *priv;
	size_t len;
	char **ap, *args[5];
	char *crypto_alg, *crypto_mode, *iv_mode, *iv_opt, *key, *dev;
	char *status_str;
	int argc, klen, error;
	uint64_t iv_offset, block_offset;

	if (params == NULL)
		return EINVAL;

	len = strlen(params) + 1;
	argc = 0;

	status_str = kstrdup(params, M_DMCRYPT);
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

	kprintf("dm_target_crypt: dev=%s, crypto_alg=%s, crypto_mode=%s, "
		"iv_mode=%s, iv_opt=%s, key=%s, iv_offset=%ju, "
		"block_offset=%ju\n",
		dev, crypto_alg, crypto_mode, iv_mode, iv_opt, key, iv_offset,
		block_offset);

	priv = kmalloc(sizeof(dm_target_crypt_config_t), M_DMCRYPT, M_WAITOK);
	if (priv == NULL) {
		kprintf("dm_target_crypt: could not allocate memory\n");
		kfree(status_str, M_DMCRYPT);
		return ENOMEM;
	}

	/* Insert dmp to global pdev list */
	if ((priv->pdev = dm_pdev_insert(dev)) == NULL) {
		kprintf("dm_target_crypt: dm_pdev_insert failed\n");
		kfree(status_str, M_DMCRYPT);
		return ENOENT;
	}

	if (strcmp(crypto_mode, "cbc") != 0) {
		kprintf("dm_target_crypt: only support 'cbc' chaining mode, "
			"invalid mode '%s'\n", crypto_mode);
		goto notsup;
	}

	if (!strcmp(crypto_alg, "aes")) {
		priv->crypto_alg = CRYPTO_AES_CBC;
		if (klen != 128 && klen != 192 && klen != 256)
			goto notsup;
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
	priv->iv_offset = iv_offset;

	*target_config = priv;

	dmv->dev_type = DM_CRYPTO_DEV;

	priv->crypto_session.cri_alg = priv->crypto_alg;
	priv->crypto_session.cri_klen = priv->crypto_klen;
	priv->crypto_session.cri_mlen = 0;

	error = hex2key(key, priv->crypto_klen >> 3,
			(u_int8_t *)priv->crypto_key);

	if (error) {
		kprintf("dm_target_crypt: hex2key failed, "
			"invalid key format\n");
		goto notsup;
	}

	if (!strcmp(iv_mode, "essiv")) {
		error = essiv_hash_mkey(priv, iv_opt);
		if (error) {
			kprintf("dm_target_crypt: essiv_hash_mkey failed\n");
			goto notsup;
		}
		priv->crypto_ivgen = essiv_ivgen;
	} else if (!strcmp(iv_mode, "plain")) {
		priv->crypto_ivgen = plain_ivgen;
	} else {
		kprintf("dm_target_crypt: only support iv_mode='essiv' and "
			"'plain', iv_mode='%s' unsupported\n",
			iv_mode);
	}

	priv->crypto_session.cri_key = (u_int8_t *)priv->crypto_key;
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

	priv->status_str = status_str;
	return 0;

notsup:
	kprintf("dm_target_crypt: ENOTSUP\n");
	kfree(status_str, M_DMCRYPT);
	return ENOTSUP;
}

/* Status routine called to get params string. */
char *
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

int
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

	dm_target_unbusy(table_en->target);

	/*
	 * Clean up the crypt config
	 *
	 * Overwrite the private information before freeing memory to
	 * avoid leaking it.
	 */
	memset(priv->status_str, 0xFF, strlen(priv->status_str));
	bzero(priv->status_str, strlen(priv->status_str));
	kfree(priv->status_str, M_DMCRYPT);

	memset(priv, 0xFF, sizeof(dm_target_crypt_config_t));
	bzero(priv, sizeof(dm_target_crypt_config_t));
	kfree(priv, M_DMCRYPT);

	return 0;
}

int
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
			      (uint64_t)makeudev(va.va_rmajor, va.va_rminor));

	return 0;
}

/* Unsupported for this target. */
int
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
 * Start IO operation, called from dmstrategy routine.
 */
int
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
dmtc_crypto_read_start(dm_target_crypt_config_t *priv, struct bio *bio)
{
	struct dmtc_helper *dmtc;
	struct cryptodesc *crd;
	struct cryptop *crp;
	struct cryptoini *cri;
	int error, i, bytes, isector, sectors, sz;
	u_char *ptr, *space;

	cri = &priv->crypto_session;

	/*
	 * Note: b_resid no good after read I/O, it will be 0, use
	 *	 b_bcount.
	 */
	bytes = bio->bio_buf->b_bcount;
	isector = (int)(bio->bio_offset / DEV_BSIZE);	/* ivgen salt base? */
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
#if 0
	if (bio->bio_buf->b_flags & B_HASBOGUS) {
		space = kmalloc(sizeof(struct dmtc_helper) + sz + bytes,
				M_DMCRYPT, M_WAITOK);
		dmtc = (struct dmtc_helper *)space;
		dmtc->free_addr = space;
		space += sizeof(struct dmtc_helper);
		dmtc->orig_buf = NULL;
		dmtc->data_buf = space + sz;
		memcpy(dmtc->data_buf, bio->bio_buf->b_data, bytes);
	} else
#endif
	{
		space = kmalloc(sizeof(struct dmtc_helper) + sz,
				M_DMCRYPT, M_WAITOK);
		dmtc = (struct dmtc_helper *)space;
		dmtc->free_addr = space;
		space += sizeof(struct dmtc_helper);
		dmtc->orig_buf = NULL;
		dmtc->data_buf = bio->bio_buf->b_data;
	}
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
		crp->crp_flags = CRYPTO_F_CBIFSYNC | CRYPTO_F_REL;

		crd->crd_alg = priv->crypto_alg;
		crd->crd_key = (caddr_t)priv->crypto_key;
		crd->crd_klen = priv->crypto_klen;

		/*
		 * Note: last argument is used to generate salt(?) and is
		 *	 a 64 bit value, but the original code passed an
		 *	 int.  Changing it now will break pre-existing
		 *	 crypt volumes.
		 */
		priv->crypto_ivgen(priv, crd->crd_iv, sizeof(crd->crd_iv),
				   (int)(isector + i));

		crd->crd_skip = 0;
		crd->crd_len = DEV_BSIZE /* XXX */;
		crd->crd_flags = CRD_F_KEY_EXPLICIT | CRD_F_IV_EXPLICIT |
				 CRD_F_IV_PRESENT;
		crd->crd_next = NULL;

		crd->crd_flags &= ~CRD_F_ENCRYPT;

		error = crypto_dispatch(crp);
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
		kfree(dmtc->free_addr, M_DMCRYPT);
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
dmtc_crypto_write_start(dm_target_crypt_config_t *priv, struct bio *bio)
{
	struct dmtc_helper *dmtc;
	struct cryptodesc *crd;
	struct cryptop *crp;
	struct cryptoini *cri;
	int error, i, bytes, isector, sectors, sz;
	u_char *ptr, *space;

	cri = &priv->crypto_session;

	/*
	 * Use b_bcount for consistency
	 */
	bytes = bio->bio_buf->b_bcount;

	isector = (int)(bio->bio_offset / DEV_BSIZE);	/* ivgen salt base? */
	sectors = bytes / DEV_BSIZE;		/* Number of sectors */
	sz = sectors * (sizeof(*crp) + sizeof(*crd));

	/*
	 * For writes and reads with bogus page don't decrypt in place.
	 */
	space = kmalloc(sizeof(struct dmtc_helper) + sz + bytes,
			M_DMCRYPT, M_WAITOK);
	dmtc = (struct dmtc_helper *)space;
	dmtc->free_addr = space;
	space += sizeof(struct dmtc_helper);
	memcpy(space + sz, bio->bio_buf->b_data, bytes);

	bio->bio_caller_info2.ptr = dmtc;
	bio->bio_buf->b_error = 0;

	dmtc->orig_buf = bio->bio_buf->b_data;
	dmtc->data_buf = space + sz;

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
		crp->crp_flags = CRYPTO_F_CBIFSYNC | CRYPTO_F_REL;

		crd->crd_alg = priv->crypto_alg;
		crd->crd_key = (caddr_t)priv->crypto_key;
		crd->crd_klen = priv->crypto_klen;

		/*
		 * Note: last argument is used to generate salt(?) and is
		 *	 a 64 bit value, but the original code passed an
		 *	 int.  Changing it now will break pre-existing
		 *	 crypt volumes.
		 */
		priv->crypto_ivgen(priv, crd->crd_iv, sizeof(crd->crd_iv),
				   (int)(isector + i));

		crd->crd_skip = 0;
		crd->crd_len = DEV_BSIZE /* XXX */;
		crd->crd_flags = CRD_F_KEY_EXPLICIT | CRD_F_IV_EXPLICIT |
				 CRD_F_IV_PRESENT;
		crd->crd_next = NULL;

		crd->crd_flags |= CRD_F_ENCRYPT;
		error = crypto_dispatch(crp);
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

	if (n == 1) {
		dmtc = bio->bio_caller_info2.ptr;
		priv = (dm_target_crypt_config_t *)bio->bio_caller_info1.ptr;

		if (bio->bio_buf->b_error) {
			bio->bio_buf->b_flags |= B_ERROR;
			kfree(dmtc->free_addr, M_DMCRYPT);
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
	kfree(dmtc->free_addr, M_DMCRYPT);
	obio = pop_bio(bio);
	biodone(obio);
}
/* END OF STRATEGY WRITE SECTION */
